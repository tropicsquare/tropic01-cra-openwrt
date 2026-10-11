#include <stdio.h>
#include <string.h>

#include "cmd_table.h"
#include "errors.h"
#include "files.h"
#include "output.h"
#include "safety.h"

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

/* the state of a pairing slot as seen by the session: CHIP_OK populated, CHIP_E_EMPTY, CHIP_E_INVALID, or an error */
static int slot_arg(const cmd_t *c, args_t *a, unsigned min, unsigned *slot)
{
	long v;
	if (a->argc != 1 || !args_number(a->argv[0], min, CHIP_PAIRING_SLOTS - 1, &v)) return cmd_usage(c, min ? "slot must be 1 to 3" : "slot must be 0 to 3");
	*slot = (unsigned)v;
	return TC_OK;
}

int cmd_pairing_write(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("pairing", "write");
	char err[CHIP_ERRLEN] = "";
	const char *in = NULL, *bad;
	uint8_t pub[33], back[32], cur[32];
	size_t n = 0;
	unsigned slot;
	chip_status_t st;
	int rc, yes = args_flag(a, "--yes-write-pairing-slot");
	if (args_value(a, "--in", &in) <= 0) return cmd_usage(c, "--in FILE is required");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc == 1 && strcmp(a->argv[0], "0") == 0)
		return out_error(TC_REFUSED, "pairing slot 0 holds the shared engineering key and is never written by this tool");
	if ((rc = slot_arg(c, a, 1, &slot))) return rc;
	if (!yes && !x->dry_run) return safety_refuse("--yes-write-pairing-slot", "a pairing slot is written once and is used up for good");
	if (file_read(in, pub, sizeof pub, &n) != 0 || n != 32) return out_error(TC_USAGE, "%s must hold exactly 32 bytes (the public key)", in);
	if ((rc = ctx_session(x))) return rc;
	st = chip_pairing_read(x->chip, slot, cur, err);
	if (st == CHIP_OK) return out_error(TC_OCCUPIED, "pairing slot %u is already written; a pairing slot is written once and never overwritten", slot);
	if (st == CHIP_E_INVALID) return out_error(TC_OCCUPIED, "pairing slot %u was invalidated and cannot be written again", slot);
	if (st != CHIP_E_EMPTY) return out_chip_error(st, "cannot tell whether the slot is empty", err); /* a chip error is not "empty" */
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): checks passed, would write the public key into pairing slot %u (IRREVERSIBLE)%s", slot, yes ? "" : "; needs --yes-write-pairing-slot");
		return TC_OK;
	}
	st = chip_pairing_write(x->chip, slot, pub, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot write the pairing slot", err);
	st = chip_pairing_read(x->chip, slot, back, err);
	if (st != CHIP_OK) return out_error(TC_VERIFY, "pairing slot %u was written but cannot be read back: %s. DO NOT CONTINUE (firmware older than 2.0.0 can fail silently)", slot, err);
	if (memcmp(back, pub, 32) != 0) return out_error(TC_VERIFY, "pairing slot %u does not read back as the key that was written. DO NOT CONTINUE", slot);
	if (g_json) printf("{\"slot\":%u,\"written\":true,\"verified\":true}\n", slot);
	else printf("pairing slot %u: written and read back correctly\n", slot);
	return TC_OK;
}

int cmd_pairing_invalidate(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("pairing", "invalidate");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	uint8_t cur[32];
	unsigned slot;
	chip_status_t st;
	int rc, yes = args_flag(a, "--yes-invalidate-pairing-slot"), yes0 = args_flag(a, "--yes-invalidate-shared-key");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = slot_arg(c, a, 0, &slot))) return rc;
	if (!yes && !x->dry_run) return safety_refuse("--yes-invalidate-pairing-slot", "invalidating a pairing slot is permanent");
	if (slot == 0 && !yes0 && !x->dry_run)
		return safety_refuse("--yes-invalidate-shared-key", "invalidating slot 0 (the shared engineering key) is permanent and can lock the chip for good");
	if ((rc = ctx_session(x))) return rc;
	if ((int)slot == x->key.slot)
		return out_error(TC_REFUSED, "this session was opened with pairing slot %u; invalidating it would cut off the key in use. Open the session with another key (--key ...)", slot);
	if (slot == 0 && x->key.slot == 0)
		return out_error(TC_REFUSED, "slot 0 can only be invalidated from a session opened with another, working pairing key (proof that the chip stays reachable)");
	st = chip_pairing_read(x->chip, slot, cur, err);
	if (st == CHIP_E_EMPTY || st == CHIP_E_INVALID) return out_error(TC_NOTFOUND, "pairing slot %u is %s: nothing to invalidate", slot, st == CHIP_E_EMPTY ? "empty" : "already invalidated");
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the pairing slot first", err);
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): checks passed, would invalidate pairing slot %u (IRREVERSIBLE)%s", slot, yes ? "" : "; needs --yes-invalidate-pairing-slot");
		return TC_OK;
	}
	st = chip_pairing_invalidate(x->chip, slot, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot invalidate the pairing slot", err);
	st = chip_pairing_read(x->chip, slot, cur, err);
	if (st != CHIP_E_INVALID) return out_error(TC_VERIFY, "pairing slot %u does not read as invalidated afterwards", slot);
	if (g_json) printf("{\"slot\":%u,\"invalidated\":true}\n", slot);
	else printf("pairing slot %u: invalidated\n", slot);
	return TC_OK;
}
