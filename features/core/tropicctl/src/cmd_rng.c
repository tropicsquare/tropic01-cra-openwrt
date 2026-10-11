#include <stdio.h>
#include <string.h>

#include "cmd_table.h"
#include "errors.h"
#include "files.h"
#include "output.h"

int cmd_rng_get(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("rng", "get");
	char err[CHIP_ERRLEN] = "", hex[2 * 255 + 1];
	const char *out = NULL, *bad;
	uint8_t buf[255];
	long n;
	int hexflag, rc, r;
	chip_status_t st;
	hexflag = args_flag(a, "--hex");
	r = args_value(a, "--out", &out);
	if (r < 0) return cmd_usage(c, "--out needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 1 || !args_number(a->argv[0], 1, 255, &n)) return cmd_usage(c, "count must be 1 to 255");
	if (hexflag && out) return cmd_usage(c, "--hex and --out cannot be combined");
	if ((rc = ctx_session(x))) return rc;
	st = chip_random(x->chip, buf, (size_t)n, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot get random bytes", err);
	if (out) {
		if (file_write(out, buf, (size_t)n) != 0) return out_error(TC_INTERNAL, "cannot write %s", out);
		if (g_json && strcmp(out, "-") != 0) {
			printf("{\"bytes\":%ld,\"file\":", n);
			json_str(stdout, out);
			printf("}\n");
		}
		memset(buf, 0, sizeof buf);
		return TC_OK;
	}
	hex_to(hex, buf, (size_t)n);
	if (g_json) printf("{\"bytes\":%ld,\"hex\":\"%s\"}\n", n, hex);
	else printf("%s\n", hex);
	memset(buf, 0, sizeof buf);
	return TC_OK;
}
