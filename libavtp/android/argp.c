/* SPDX-License-Identifier: BSD-3-Clause */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>

#define ARGP_MAX_OPTS 32

int argp_parse(const struct argp *argp, int argc, char **argv,
	       unsigned flags, int *arg_index, void *input)
{
	struct option longopts[ARGP_MAX_OPTS + 1];
	char optstring[2 * ARGP_MAX_OPTS + 2];
	size_t n = 0, s = 0;
	int c;

	(void) flags;
	(void) input;

	optstring[s++] = ':';
	for (const struct argp_option *o = argp->options;
	     o && o->name && n < ARGP_MAX_OPTS; o++, n++) {
		longopts[n].name = o->name;
		longopts[n].has_arg = o->arg ? required_argument : no_argument;
		longopts[n].flag = NULL;
		longopts[n].val = o->key;
		optstring[s++] = (char) o->key;
		if (o->arg)
			optstring[s++] = ':';
	}
	memset(&longopts[n], 0, sizeof(longopts[n]));
	optstring[s] = '\0';

	while ((c = getopt_long(argc, argv, optstring, longopts, NULL)) != -1) {
		if (c == '?' || c == ':') {
			fprintf(stderr, "Usage: %s [options]\n", argv[0]);
			for (const struct argp_option *o = argp->options;
			     o && o->name; o++)
				fprintf(stderr, "  -%c, --%-20s %s\n", o->key,
					o->name, o->doc ? o->doc : "");
			exit(EXIT_FAILURE);
		}
		if (argp->parser(c, optarg, NULL) != 0)
			exit(EXIT_FAILURE);
	}

	if (arg_index)
		*arg_index = optind;
	return 0;
}
