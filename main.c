#define _POSIX_C_SOURCE 2
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "lmc.h"

extern char* optarg;

int main(int argc, char* argv[])
{
	lmc_int memory_size = 100;
	int debug = 0;

	int c;
	while ((c = getopt(argc, argv, "dm:")) != -1) {
		switch (c) {
			case 'm':
				memory_size = (lmc_int)atoi(optarg);
				if (memory_size > 100) {
					fprintf(stderr, "-m requires a memory size at or below 100\n");
					return -1;
				}
				break;
			case 'd':
				debug = 1;
				break;
			case '?':
				return -1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "Input file required\n");
		return -1;
	}

	if (debug) {
		fprintf(stderr, "Running lmc in debug mode with %i bytes of memory\n", memory_size);
	}

	struct lmc_state* state = lmc_alloc(memory_size, NULL);
	if (state == NULL) {
		fprintf(stderr, "%s", strerror(errno));
		return -1;
	}

	int success = 1;

	while (optind < argc) {
		FILE* input = fopen(argv[optind++], "r");
		if (input == NULL) {
			fprintf(stderr, "Cannot open input %s: %s\n", argv[optind-1], strerror(errno));
			success = 0;
			break;
		}

		int load_result = lmc_load_file(state, argv[optind-1], input);
		fclose(input);

		if (load_result == 0) {
			while(lmc_execute_instruction(state, stdin, stdout)) {
				if (debug)
					lmc_debugprint(state, stderr);
			}
		} else {
			success = 0;
			break;
		}
	}

	if (debug) {
		if (success)
			fprintf(stderr, "LMC completed successfully\n");
		else
			fprintf(stderr, "LMC aborted\n");
	}

	lmc_destroy(state, NULL);

	if (success)
		return 0;
	else
		return -2;
}
