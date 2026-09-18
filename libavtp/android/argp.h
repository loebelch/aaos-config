/* SPDX-License-Identifier: BSD-3-Clause */
/* Minimaler argp-Ersatz auf getopt_long-Basis für bionic (Android). */
#ifndef AVTP_ANDROID_ARGP_H
#define AVTP_ANDROID_ARGP_H

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int error_t;
struct argp_state;

struct argp_option {
	const char *name;
	int key;
	const char *arg;
	int flags;
	const char *doc;
	int group;
};

typedef error_t (*argp_parser_t)(int key, char *arg, struct argp_state *state);

struct argp {
	const struct argp_option *options;
	argp_parser_t parser;
	const char *args_doc;
	const char *doc;
};

int argp_parse(const struct argp *argp, int argc, char **argv,
	       unsigned flags, int *arg_index, void *input);

#ifdef __cplusplus
}
#endif
#endif /* AVTP_ANDROID_ARGP_H */
