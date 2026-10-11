#include <stdio.h>
#include <string.h>

#include "cmd_table.h"
#include "errors.h"
#include "files.h"
#include "output.h"

static int slot_arg(const cmd_t *c, args_t *a, unsigned *slot)
{
	long v;
	if (a->argc != 1 || !args_number(a->argv[0], 0, CHIP_RMEM_SLOTS - 1, &v)) return cmd_usage(c, "slot must be 0 to 511");
	*slot = (unsigned)v;
	return TC_OK;
}

int cmd_mem_list(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("mem", "list");
	char err[CHIP_ERRLEN] = "";
	const char *range = NULL, *bad;
	uint8_t buf[CHIP_RMEM_MAX];
	long lo = 0, hi = CHIP_RMEM_SLOTS - 1, first = 1;
	int rc, r = args_value(a, "--range", &range);
	if (r < 0) return cmd_usage(c, "--range needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (range) {
		char tmp[32], *dash;
		snprintf(tmp, sizeof tmp, "%s", range);
		dash = strchr(tmp, '-');
		if (!dash) return cmd_usage(c, "range is A-B");
		*dash = '\0';
		if (!args_number(tmp, 0, CHIP_RMEM_SLOTS - 1, &lo) || !args_number(dash + 1, lo, CHIP_RMEM_SLOTS - 1, &hi))
			return cmd_usage(c, "range is A-B with 0 <= A <= B <= 511");
	}
	if ((rc = ctx_session(x))) return rc;
	if (g_json) printf("{\"slots\":[");
	for (long s = lo; s <= hi; s++) {
		size_t len = 0;
		chip_status_t st = chip_mem_read(x->chip, (unsigned)s, buf, &len, err);
		if (st == CHIP_E_EMPTY) continue;
		if (st != CHIP_OK) return out_chip_error(st, "cannot read R-MEM", err);
		if (g_json) {
			printf("%s{\"slot\":%ld,\"bytes\":%zu}", first ? "" : ",", s, len);
			first = 0;
		} else {
			printf("slot %ld: %zu bytes\n", s, len);
		}
	}
	if (g_json) printf("]}\n");
	return TC_OK;
}

int cmd_mem_read(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("mem", "read");
	char err[CHIP_ERRLEN] = "", hex[2 * CHIP_RMEM_MAX + 1];
	const char *out = NULL, *bad;
	uint8_t buf[CHIP_RMEM_MAX];
	size_t len = 0;
	unsigned slot;
	chip_status_t st;
	int rc, r = args_value(a, "--out", &out);
	if (r < 0) return cmd_usage(c, "--out needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = slot_arg(c, a, &slot))) return rc;
	if ((rc = ctx_session(x))) return rc;
	st = chip_mem_read(x->chip, slot, buf, &len, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the slot", err);
	if (out) {
		if (file_write(out, buf, len) != 0) return out_error(TC_INTERNAL, "cannot write %s", out);
		if (g_json && strcmp(out, "-") != 0) {
			printf("{\"slot\":%u,\"bytes\":%zu,\"file\":", slot, len);
			json_str(stdout, out);
			printf("}\n");
		}
		return TC_OK;
	}
	hex_to(hex, buf, len);
	if (g_json) printf("{\"slot\":%u,\"bytes\":%zu,\"hex\":\"%s\"}\n", slot, len, hex);
	else printf("%s\n", hex);
	return TC_OK;
}

int cmd_mem_write(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("mem", "write");
	char err[CHIP_ERRLEN] = "";
	const char *in = NULL, *bad;
	uint8_t data[CHIP_RMEM_MAX], back[CHIP_RMEM_MAX];
	size_t len = 0, blen = 0;
	unsigned slot;
	chip_status_t st;
	int rc, replace = args_flag(a, "--replace"), r = args_value(a, "--in", &in);
	if (r < 0 || !in) return cmd_usage(c, "--in FILE is required");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = slot_arg(c, a, &slot))) return rc;
	rc = file_read(in, data, sizeof data, &len);
	if (rc == -2) return out_error(TC_USAGE, "%s is longer than %d bytes", in, CHIP_RMEM_MAX);
	if (rc != 0 || len == 0) return out_error(TC_USAGE, "cannot read %s (it must hold 1 to %d bytes)", in, CHIP_RMEM_MAX);
	if ((rc = ctx_session(x))) return rc;
	st = chip_mem_read(x->chip, slot, back, &blen, err);
	if (st == CHIP_OK && !replace)
		return out_error(TC_OCCUPIED, "R-MEM slot %u already holds %zu bytes; use --replace to erase and rewrite it", slot, blen);
	if (st != CHIP_OK && st != CHIP_E_EMPTY) return out_chip_error(st, "cannot check the slot first", err);
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): would write %zu bytes to R-MEM slot %u%s", len, slot,
		           st == CHIP_OK ? " after erasing it" : "");
		return TC_OK;
	}
	if (st == CHIP_OK && (st = chip_mem_erase(x->chip, slot, err)) != CHIP_OK) return out_chip_error(st, "cannot erase the slot", err);
	st = chip_mem_write(x->chip, slot, data, len, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot write the slot", err);
	st = chip_mem_read(x->chip, slot, back, &blen, err);
	if (st != CHIP_OK) return out_chip_error(CHIP_E_VERIFY, "the slot was written but cannot be read back", err);
	if (blen != len || memcmp(back, data, len) != 0)
		return out_error(TC_VERIFY, "R-MEM slot %u does not read back as written", slot);
	if (g_json) printf("{\"slot\":%u,\"bytes\":%zu,\"verified\":true}\n", slot, len);
	else printf("slot %u: wrote %zu bytes and read them back\n", slot, len);
	return TC_OK;
}

int cmd_mem_erase(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("mem", "erase");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	uint8_t buf[CHIP_RMEM_MAX];
	size_t len = 0;
	unsigned slot;
	chip_status_t st;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = slot_arg(c, a, &slot))) return rc;
	if ((rc = ctx_session(x))) return rc;
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): would erase R-MEM slot %u", slot);
		return TC_OK;
	}
	st = chip_mem_erase(x->chip, slot, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot erase the slot", err);
	st = chip_mem_read(x->chip, slot, buf, &len, err);
	if (st != CHIP_E_EMPTY) return out_error(TC_VERIFY, "R-MEM slot %u still reads as populated after the erase", slot);
	if (g_json) printf("{\"slot\":%u,\"erased\":true}\n", slot);
	else printf("slot %u: erased\n", slot);
	return TC_OK;
}
