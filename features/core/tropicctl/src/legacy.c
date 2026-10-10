/*
 * legacy.c -- `lt-util` compatibility. Started under the name lt-util the program accepts the old flags and keeps
 * their behaviour and message texts (callers parse them), with the exit status 0 or 1 the old tool had.
 * Commands not available yet in this build say so and exit 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_table.h"
#include "errors.h"
#include "keys.h"
#include "output.h"

static void usage(void)
{
	printf("\nUsage (lt-util compatibility mode of tropicctl):\n\n"
	       "\t./lt-util -i                              # Print chip identification\n"
	       "\t./lt-util -r    <count> <file>            # Random  - Get 2-255 random bytes and store them into file\n"
	       "\t./lt-util -e -i <slot> <file>             # ECC key - Install an Ed25519 private key from file into a slot (0-31)\n"
	       "\t./lt-util -e -g <slot>                    # ECC key - Generate a P-256 private key in a slot (0-31)\n"
	       "\t./lt-util -e -d <slot> <file>             # ECC key - Download the public key of a slot into file\n"
	       "\t./lt-util -e -c <slot>                    # ECC key - Clear (erase) an ECC slot\n"
	       "\t./lt-util -e -s <slot> <file1> <file2>    # ECC key - EdDSA-sign file1, store the signature in file2\n"
	       "\t./lt-util -m -s <slot> <file>             # Memory  - Store content of file into memory slot (0-511)\n"
	       "\t./lt-util -m -r <slot> <file>             # Memory  - Read content of memory slot (0-511) into file\n"
	       "\t./lt-util -m -e <slot>                    # Memory  - Erase content of memory slot (0-511)\n"
	       "\t./lt-util -k -r <slot> <file>             # Pairing key - Read the public pairing key of a slot (0-3)\n"
	       "\t./lt-util -k -w <slot> <file> --irreversible  # Pairing key - IRREVERSIBLE: write a 32-byte public key into an empty slot 1-3\n"
	       "\t./lt-util -x -r                           # Survey  - Print every R-config and I-config object\n"
	       "\t./lt-util -x -f                           # Survey  - Print the firmware version bytes\n\n"
	       "\t All commands return 0 if success, otherwise 1.\n"
	       "\t The old commands keep their behaviour; new commands are available as tropicctl.\n\n");
}

static int run(ctx_t *x, const cmd_t *c, int argc, char **argv)
{
	args_t a = {argc, argv};
	return c->fn(x, &a) == TC_OK ? 0 : 1;
}

int legacy_main(int argc, char **argv, ctx_t *x)
{
	char *v[8];

	const char *quiet = getenv("TROPICCTL_NO_DEPRECATION");
	if (!quiet || !*quiet)
		fprintf(stderr, "lt-util: note: this is now tropicctl in compatibility mode (see: tropicctl help); "
		                "set TROPICCTL_NO_DEPRECATION=1 to silence this line\n");
	if (getenv("LT_UTIL_FORCE_SHARED_KEY")) x->key_source = "shared";
	if (argc == 1) {
		usage();
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "-i") == 0) {
		char desc[96];
		pairing_key_t k;
		if (keys_select(x->key_source, &k, desc, sizeof desc)) return 1; /* the old tool announced the key for every command */
		memset(&k, 0, sizeof k);
		return run(x, cmd_find("info", "chip"), 0, v);
	}
	if (argc == 3 && strcmp(argv[1], "-x") == 0 && strcmp(argv[2], "-r") == 0) return run(x, cmd_find("config", "dump"), 0, v);
	if (argc == 3 && strcmp(argv[1], "-x") == 0 && strcmp(argv[2], "-f") == 0) return run(x, cmd_find("info", "fw"), 0, v);
	if (argc == 4 && strcmp(argv[1], "-r") == 0) {
		long n;
		if (!args_number(argv[2], 2, 255, &n)) {
			out_error(TC_USAGE, "count must be 2 to 255");
			return 1;
		}
		v[0] = argv[2]; v[1] = "--out"; v[2] = argv[3];
		return run(x, cmd_find("rng", "get"), 3, v);
	}
	if (argc >= 4 && strcmp(argv[1], "-m") == 0) {
		if (strcmp(argv[2], "-e") == 0 && argc == 4) {
			v[0] = argv[3];
			return run(x, cmd_find("mem", "erase"), 1, v);
		}
		if (argc == 5 && (strcmp(argv[2], "-s") == 0 || strcmp(argv[2], "-r") == 0)) {
			v[0] = argv[3]; v[1] = strcmp(argv[2], "-s") == 0 ? "--in" : "--out"; v[2] = argv[4];
			return run(x, cmd_find("mem", strcmp(argv[2], "-s") == 0 ? "write" : "read"), 3, v);
		}
	}
	if (argc >= 4 && strcmp(argv[1], "-e") == 0) {
		if (argc == 4 && strcmp(argv[2], "-g") == 0) {
			v[0] = argv[3]; v[1] = "--curve"; v[2] = "p256";
			return run(x, cmd_find("ecc", "generate"), 3, v);
		}
		if (argc == 4 && strcmp(argv[2], "-c") == 0) {
			v[0] = argv[3]; v[1] = "--yes-erase-ecc-key"; /* the old tool erased without a confirmation */
			return run(x, cmd_find("ecc", "erase"), 2, v);
		}
		if (argc == 5 && strcmp(argv[2], "-d") == 0) {
			v[0] = argv[3]; v[1] = "--out"; v[2] = argv[4];
			return run(x, cmd_find("ecc", "pub"), 3, v);
		}
		if (argc == 5 && strcmp(argv[2], "-i") == 0) {
			v[0] = argv[3]; v[1] = "--curve"; v[2] = "ed25519"; v[3] = "--in"; v[4] = argv[4];
			return run(x, cmd_find("ecc", "store"), 5, v);
		}
		if (argc == 6 && strcmp(argv[2], "-s") == 0) {
			v[0] = argv[3]; v[1] = "--in"; v[2] = argv[4]; v[3] = "--out"; v[4] = argv[5]; v[5] = "--alg"; /* v has 6 slots */
			{
				char *w[8] = {argv[3], "--in", argv[4], "--out", argv[5], "--alg", "eddsa", NULL};
				return run(x, cmd_find("ecc", "sign"), 7, w);
			}
		}
	}
	if (argc == 6 && strcmp(argv[1], "-k") == 0 && strcmp(argv[2], "-w") == 0) {
		char *w[5];
		if (strcmp(argv[5], "--irreversible") != 0) {
			fprintf(stderr, "ERROR This write is irreversible: pass the literal argument --irreversible to confirm\n");
			return 1;
		}
		w[0] = argv[3]; w[1] = "--in"; w[2] = argv[4]; w[3] = "--yes-write-pairing-slot";
		return run(x, cmd_find("pairing", "write"), 4, w);
	}
	if (argc == 5 && strcmp(argv[1], "-k") == 0 && strcmp(argv[2], "-r") == 0) {
		v[0] = argv[3]; v[1] = "--out"; v[2] = argv[4];
		return run(x, cmd_find("pairing", "read"), 3, v);
	}
	fprintf(stderr, "ERROR wrong parameters entered\n");
	return 1;
}
