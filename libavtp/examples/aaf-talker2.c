/*
 * aaf-talker2.c — getakteter AAF-Talker mit mehreren Frames pro AVTPDU
 *
 * Drop-in-Ersatz fuer examples/aaf-talker.c aus libavtp. Zwei Unterschiede,
 * die fuer echten Betrieb entscheidend sind:
 *
 *  1. MEHRERE FRAMES PRO AVTPDU (-n, Default 6).
 *     Das Original packt genau einen Frame je Paket: bei 48 kHz sind das
 *     48.000 Pakete/s. Mit 6 Frames sind es 8.000 Pakete/s — der uebliche
 *     Wert fuer SR Class A.
 *
 *  2. EIGENES PACING.
 *     Das Original sendet so schnell, wie es stdin lesen kann; das Takten
 *     war an 'arecord' delegiert. Hier wird der Sendezeitpunkt einmal
 *     verankert und danach um ein festes Intervall fortgeschrieben
 *     (clock_nanosleep mit TIMER_ABSTIME).
 *
 * Nebeneffekt von 2.: die AVTP-Timestamps sind konstruktionsbedingt
 * streng monoton, weil sie aus dem Sendeplan abgeleitet werden und nicht
 * bei jedem Paket neu aus der Uhr gelesen werden. Ein Rueckwaertssprung
 * der Systemuhr (z.B. durch phc2sys) kann damit keine Timestamps mehr
 * rueckwaerts laufen lassen — genau der Fall, an dem GStreamers
 * avtpaafdepay dauerhaft um 4,295 s verrutscht.
 *
 * Bauen (Host):
 *   cc -O2 -Wall -o aaf-talker2 aaf-talker2.c examples/common.c \
 *      -Iinclude -I. -Lbuild -lavtp
 *
 * Benutzung:
 *   cat audio_48k_s16le_stereo.raw | \
 *     ./aaf-talker2 -i eth0 -d 91:e0:f0:00:fe:01 -m 50 -p 3 -n 6
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "avtp.h"
#include "avtp_aaf.h"
#include "examples/common.h"

#define STREAM_ID               0xAABBCCDDEEFF0001ULL
#define SAMPLE_SIZE             2       /* Bytes je Sample (S16) */
#define NUM_CHANNELS            2
#define NSEC_PER_SEC            1000000000ULL
#define NSEC_PER_MSEC           1000000ULL

static char ifname[IFNAMSIZ];
static uint8_t macaddr[ETH_ALEN];
static int priority = -1;
static int max_transit_time = 50;       /* ms */
static int frames_per_pdu = 6;          /* SR Class A */
static int sample_rate = 48000;
static int free_run;                    /* 1 = Quelle taktet, kein eigenes Pacing */

static const struct option long_opts[] = {
	{ "dst-addr",          required_argument, NULL, 'd' },
	{ "ifname",            required_argument, NULL, 'i' },
	{ "max-transit-time",  required_argument, NULL, 'm' },
	{ "prio",              required_argument, NULL, 'p' },
	{ "frames-per-pdu",    required_argument, NULL, 'n' },
	{ "rate",              required_argument, NULL, 'r' },
	{ "free-run",          no_argument,       NULL, 'F' },
	{ "help",              no_argument,       NULL, 'h' },
	{ 0, 0, 0, 0 }
};

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s -i IFNAME -d MACADDR [options]\n"
		"  -i, --ifname IFNAME            Netzwerk-Interface\n"
		"  -d, --dst-addr MACADDR         Ziel-MAC des Streams\n"
		"  -m, --max-transit-time MSEC    Max Transit Time in ms (50)\n"
		"  -p, --prio NUM                 SO_PRIORITY des Sockets\n"
		"  -n, --frames-per-pdu NUM       Frames je AVTPDU (6)\n"
		"  -r, --rate HZ                  44100 | 48000 | 96000 (48000)\n"
		"  -F, --free-run                 kein eigenes Pacing; die Quelle\n"
		"                                 taktet (arecord, snd-aloop, ...)\n",
		prog);
}

static int rate_to_nsr(int rate)
{
	switch (rate) {
	case 44100: return AVTP_AAF_PCM_NSR_44_1KHZ;
	case 48000: return AVTP_AAF_PCM_NSR_48KHZ;
	case 96000: return AVTP_AAF_PCM_NSR_96KHZ;
	default:    return -1;
	}
}

static int parse_args(int argc, char *argv[])
{
	int c;

	while ((c = getopt_long(argc, argv, "d:i:m:p:n:r:Fh",
				long_opts, NULL)) != -1) {
		switch (c) {
		case 'd':
			if (sscanf(optarg, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
				   &macaddr[0], &macaddr[1], &macaddr[2],
				   &macaddr[3], &macaddr[4], &macaddr[5]) != 6) {
				fprintf(stderr, "Invalid MAC address\n");
				return -1;
			}
			break;
		case 'i':
			strncpy(ifname, optarg, sizeof(ifname) - 1);
			break;
		case 'm':
			max_transit_time = atoi(optarg);
			break;
		case 'p':
			priority = atoi(optarg);
			break;
		case 'n':
			frames_per_pdu = atoi(optarg);
			break;
		case 'r':
			sample_rate = atoi(optarg);
			break;
		case 'F':
			free_run = 1;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return -1;
		}
	}

	if (ifname[0] == '\0') {
		fprintf(stderr, "Missing -i IFNAME\n");
		return -1;
	}
	if (frames_per_pdu < 1 || frames_per_pdu > 1024) {
		fprintf(stderr, "frames-per-pdu out of range\n");
		return -1;
	}
	if (rate_to_nsr(sample_rate) < 0) {
		fprintf(stderr, "Unsupported rate %d\n", sample_rate);
		return -1;
	}
	return 0;
}

static int init_pdu(struct avtp_stream_pdu *pdu, size_t data_len)
{
	int res;

	res = avtp_aaf_pdu_init(pdu);
	if (res < 0)
		return -1;

	if (avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_TV, 1) < 0 ||
	    avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_STREAM_ID, STREAM_ID) < 0 ||
	    avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_FORMAT,
			     AVTP_AAF_FORMAT_INT_16BIT) < 0 ||
	    avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_NSR,
			     rate_to_nsr(sample_rate)) < 0 ||
	    avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_CHAN_PER_FRAME,
			     NUM_CHANNELS) < 0 ||
	    avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_BIT_DEPTH, 16) < 0 ||
	    avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_STREAM_DATA_LEN, data_len) < 0 ||
	    avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_SP,
			     AVTP_AAF_PCM_SP_NORMAL) < 0)
		return -1;

	return 0;
}

/* stdin bis zum vollen Block lesen; 0 = EOF, -1 = Fehler */
static int read_block(uint8_t *buf, size_t len)
{
	size_t done = 0;

	while (done < len) {
		ssize_t n = read(STDIN_FILENO, buf + done, len - done);

		if (n == 0) {
			if (done == 0)
				return 0;
			/* Rest mit Stille auffuellen */
			memset(buf + done, 0, len - done);
			return 1;
		}
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("Failed to read from stdin");
			return -1;
		}
		done += (size_t) n;
	}
	return 1;
}

int main(int argc, char *argv[])
{
	int fd, res, ret = 1;
	struct sockaddr_ll sk_addr;
	struct avtp_stream_pdu *pdu = NULL;
	struct timespec tx;
	uint64_t tx_ns, interval_ns;
	size_t data_len, pdu_size;
	uint8_t seq_num = 0;

	if (parse_args(argc, argv) < 0)
		return 1;

	data_len = (size_t) frames_per_pdu * NUM_CHANNELS * SAMPLE_SIZE;
	pdu_size = sizeof(struct avtp_stream_pdu) + data_len;
	interval_ns = (uint64_t) frames_per_pdu * NSEC_PER_SEC / sample_rate;

	fprintf(stderr,
		"AAF talker: %d Hz, %d ch, %d Frames/PDU -> %.0f Pakete/s, "
		"AVTPDU %zu Byte, Intervall %" PRIu64 " ns%s\n",
		sample_rate, NUM_CHANNELS, frames_per_pdu,
		(double) sample_rate / frames_per_pdu, pdu_size, interval_ns,
		free_run ? " [free-run: Quelle taktet]" : "");

	pdu = malloc(pdu_size);
	if (!pdu) {
		fprintf(stderr, "Failed to allocate PDU\n");
		return 1;
	}

	fd = create_talker_socket(priority);
	if (fd < 0)
		goto out_free;

	if (setup_socket_address(fd, ifname, macaddr, ETH_P_TSN, &sk_addr) < 0)
		goto out_close;

	if (init_pdu(pdu, data_len) < 0)
		goto out_close;

	/* Sendeplan EINMAL verankern, danach nur noch fortschreiben.
	 * Dadurch sind die AVTP-Timestamps exakt aequidistant und streng
	 * monoton — unabhaengig davon, was die Systemuhr spaeter tut.
	 */
	if (clock_gettime(CLOCK_REALTIME, &tx) < 0) {
		perror("Failed to get time");
		goto out_close;
	}
	tx_ns = (uint64_t) tx.tv_sec * NSEC_PER_SEC + tx.tv_nsec;
	tx_ns += 100 * NSEC_PER_MSEC;       /* etwas Vorlauf */

	while (1) {
		uint32_t avtp_time;
		ssize_t n;

		res = read_block(pdu->avtp_payload, data_len);
		if (res < 0)
			goto out_close;
		if (res == 0)
			break;

		avtp_time = (uint32_t)
			((tx_ns + (uint64_t) max_transit_time * NSEC_PER_MSEC)
			 % (1ULL << 32));

		if (avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_TIMESTAMP,
				     avtp_time) < 0)
			goto out_close;
		if (avtp_aaf_pdu_set(pdu, AVTP_AAF_FIELD_SEQ_NUM,
				     seq_num++) < 0)
			goto out_close;

		/* Im free-run-Modus taktet die Quelle: read_block() oben
		 * blockiert bereits im Takt von arecord bzw. snd-aloop.
		 * Zusaetzlich zu schlafen wuerde zwei Uhren gegeneinander
		 * laufen lassen.
		 */
		if (!free_run) {
			tx.tv_sec = tx_ns / NSEC_PER_SEC;
			tx.tv_nsec = tx_ns % NSEC_PER_SEC;
			res = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,
					      &tx, NULL);
			if (res != 0 && res != EINTR) {
				errno = res;
				perror("clock_nanosleep");
				goto out_close;
			}
		}

		n = sendto(fd, pdu, pdu_size, 0,
			   (struct sockaddr *) &sk_addr, sizeof(sk_addr));
		if (n < 0) {
			perror("Failed to send data");
			goto out_close;
		}
		if ((size_t) n != pdu_size)
			fprintf(stderr, "wrote %zd bytes, expected %zu\n",
				n, pdu_size);

		tx_ns += interval_ns;
	}

	ret = 0;

out_close:
	close(fd);
out_free:
	free(pdu);
	return ret;
}
