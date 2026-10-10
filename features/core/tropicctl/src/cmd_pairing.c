#include <stdio.h>
#include <string.h>

#include "cmd_table.h"
#include "errors.h"
#include "files.h"
#include "output.h"

int cmd_pairing_list(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("pairing", "list");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	uint8_t pub[32];
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = ctx_session(x))) return rc;
	if (g_json) printf("{\"pairing_slots\":[");
	for (unsigned s = 0; s < CHIP_PAIRING_SLOTS; s++) {
		const char *state;
		chip_status_t st = chip_pairing_read(x->chip, s, pub, err);
		if (st == CHIP_OK) state = "populated";
		else if (st == CHIP_E_EMPTY) state = "empty";
		else if (st == CHIP_E_INVALID) state = "invalidated";
		else return out_chip_error(st, "cannot read a pairing slot", err);
		if (g_json) printf("%s{\"slot\":%u,\"state\":\"%s\"}", s ? "," : "", s, state);
		else printf("pairing slot %u: %s\n", s, state);
	}
	if (g_json) {
		printf("],\"key\":");
		json_str(stdout, x->key_desc);
		printf("}\n");
	}
	return TC_OK;
}

int cmd_pairing_read(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("pairing", "read");
	char err[CHIP_ERRLEN] = "", hex[65];
	const char *out = NULL, *bad;
	uint8_t pub[32];
	long slot;
	chip_status_t st;
	int rc, r = args_value(a, "--out", &out);
	if (r < 0) return cmd_usage(c, "--out needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 1 || !args_number(a->argv[0], 0, CHIP_PAIRING_SLOTS - 1, &slot)) return cmd_usage(c, "slot must be 0 to 3");
	if ((rc = ctx_session(x))) return rc;
	st = chip_pairing_read(x->chip, (unsigned)slot, pub, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the pairing slot", err);
	if (out) {
		if (file_write(out, pub, 32) != 0) return out_error(TC_INTERNAL, "cannot write %s", out);
		if (g_json && strcmp(out, "-") != 0) printf("{\"slot\":%ld,\"bytes\":32}\n", slot);
		return TC_OK;
	}
	hex_to(hex, pub, 32);
	if (g_json) printf("{\"slot\":%ld,\"public_key\":\"%s\"}\n", slot, hex);
	else printf("%s\n", hex);
	return TC_OK;
}
