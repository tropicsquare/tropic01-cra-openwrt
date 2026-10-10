#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cfgfile.h"
#include "cmd_table.h"
#include "errors.h"
#include "output.h"
#include "safety.h"
#include "uap.h"

static int is_uap(const char *name) { return uap_kind(name) != UAP_NONE; }

static int read_objs(ctx_t *x, chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT])
{
	char err[CHIP_ERRLEN] = "";
	chip_status_t st = chip_config_dump(x->chip, o, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the configuration", err);
	return TC_OK;
}

int cmd_config_dump(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("config", "dump");
	const char *bad;
	chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT];
	int rc, decode = args_flag(a, "--decode");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if (decode && g_json) return cmd_usage(c, "--decode is for the text output");
	if ((rc = ctx_session(x))) return rc;
	if ((rc = read_objs(x, o))) return rc;
	if (g_json) {
		printf("{\"r_config\":[");
		for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) printf("%s{\"name\":\"%s\",\"value\":\"0x%08x\"}", i ? "," : "", o[i].name, o[i].r_value);
		printf("],\"i_config\":[");
		for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) printf("%s{\"name\":\"%s\",\"value\":\"0x%08x\"}", i ? "," : "", o[i].name, o[i].i_value);
		printf("]}\n");
		return TC_OK;
	}
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) {
		printf("R %s 0x%08x\n", o[i].name, o[i].r_value);
		if (decode) uap_describe(stdout, o[i].name, o[i].r_value);
	}
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) {
		printf("I %s 0x%08x\n", o[i].name, o[i].i_value);
		if (decode) uap_describe(stdout, o[i].name, o[i].i_value);
	}
	return TC_OK;
}

int cmd_config_get(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("config", "get");
	const char *bad;
	chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT];
	int rc, i;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 1) return cmd_usage(c, "one object name is needed");
	if ((rc = ctx_session(x))) return rc;
	if ((rc = read_objs(x, o))) return rc;
	i = cfg_find(o, a->argv[0]);
	if (i < 0) return out_error(TC_NOTFOUND, "no configuration object named %s (see: tropicctl config dump)", a->argv[0]);
	if (g_json) printf("{\"name\":\"%s\",\"r\":\"0x%08x\",\"i\":\"0x%08x\"}\n", o[i].name, o[i].r_value, o[i].i_value);
	else printf("R %s 0x%08x\nI %s 0x%08x\n", o[i].name, o[i].r_value, o[i].name, o[i].i_value);
	return TC_OK;
}

int cmd_config_diff(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("config", "diff");
	char err[240] = "";
	const char *in = NULL, *bad;
	chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT];
	uint32_t w[CHIP_CONFIG_OBJ_CNT];
	int rc, check = args_flag(a, "--check"), changed = 0;
	if (args_value(a, "--in", &in) <= 0) return cmd_usage(c, "--in FILE is required");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = ctx_session(x))) return rc;
	if ((rc = read_objs(x, o))) return rc;
	if (cfg_parse(in, o, w, err, sizeof err) != 0) return out_error(TC_USAGE, "%s", err);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) {
		if (o[i].r_value == w[i]) continue;
		changed++;
		printf("%s chip=0x%08x file=0x%08x\n", o[i].name, o[i].r_value, w[i]);
		uap_describe_diff(stdout, o[i].name, o[i].r_value, w[i]);
	}
	if (!changed) printf("the chip's R-config equals %s\n", in);
	return check && changed ? out_error(TC_VERIFY, "%d object(s) differ", changed) : TC_OK;
}

/* the new file for tier 0: remove pairing key N's counterpart 0 (the shared key) from every access object but PING */
int cmd_config_restrict_shared(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("config", "restrict-shared");
	char err[CHIP_ERRLEN] = "";
	const char *out = "-", *unit = NULL, *bad;
	chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT];
	uint32_t w[CHIP_CONFIG_OBJ_CNT];
	uint8_t pub[32];
	long u;
	chip_status_t st;
	int rc, ew, rd, changed = 0;
	if (args_value(a, "--unit-slot", &unit) <= 0 || !args_number(unit, 1, 3, &u)) return cmd_usage(c, "--unit-slot 1|2|3 is required");
	if (args_value(a, "--out", &out) < 0) return cmd_usage(c, "--out needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if ((rc = ctx_session(x))) return rc;
	if ((rc = read_objs(x, o))) return rc;
	st = chip_pairing_read(x->chip, (unsigned)u, pub, err);
	if (st == CHIP_E_EMPTY || st == CHIP_E_INVALID) return out_error(TC_REFUSED, "pairing slot %ld holds no usable key: restricting the shared key first would lock the chip", u);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the unit key's pairing slot", err);
	ew = cfg_find(o, "UAP_R_CONFIG_WRITE_ERASE");
	rd = cfg_find(o, "UAP_R_CONFIG_READ");
	if (!uap_allows(o[ew].r_value, UAP_SINGLE, (int)u, 0) || !uap_allows(o[rd].r_value, UAP_SINGLE, (int)u, 0))
		return out_error(TC_REFUSED, "pairing key %ld does not hold the right to read and rewrite the configuration today; restricting the shared key would make the restriction impossible to undo", u);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) {
		uap_kind_t k = uap_kind(o[i].name);
		w[i] = o[i].r_value;
		if (k == UAP_NONE || strcmp(o[i].name, "TR01_CFG_UAP_PING") == 0) continue; /* PING stays: anyone may check the chip answers */
		w[i] = uap_clear_key(w[i], k, 0);
		if (w[i] != o[i].r_value) changed++;
	}
	if (cfg_write(out, o, w, "tropicctl config restrict-shared: pairing key 0 (shared) removed from every access object except PING") != 0)
		return out_error(TC_INTERNAL, "cannot write %s", out);
	out_notice("%d access object(s) change; review with: tropicctl config diff --in %s; apply with: tropicctl config rewrite --in %s --yes-rewrite-r-config",
	           changed, strcmp(out, "-") ? out : "FILE", strcmp(out, "-") ? out : "FILE");
	return TC_OK;
}

static void stamp(char *buf, size_t n)
{
	time_t t = time(NULL);
	struct tm tmv;
	localtime_r(&t, &tmv);
	strftime(buf, n, "%Y%m%d-%H%M%S", &tmv);
}

/* rewrite (H): the whole sequence for `config rewrite` and `config restore` */
static int do_rewrite(ctx_t *x, args_t *a, const cmd_t *c, int restore)
{
	char err[CHIP_ERRLEN] = "", perr[240] = "", bk[300], ts[32];
	const char *in = NULL, *backup = NULL, *bad;
	chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT], after[CHIP_CONFIG_OBJ_CNT];
	uint32_t w[CHIP_CONFIG_OBJ_CNT], cur[CHIP_CONFIG_OBJ_CNT];
	chip_status_t st;
	int rc, stage = 0, changed = 0, ew, rd;
	int yes = args_flag(a, "--yes-rewrite-r-config"), incl = args_flag(a, "--include-non-access");
	if (args_value(a, "--in", &in) <= 0) return cmd_usage(c, "--in FILE is required");
	if (args_value(a, "--backup", &backup) < 0) return cmd_usage(c, "--backup needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if (!yes && !x->dry_run) return safety_refuse("--yes-rewrite-r-config", "rewriting the R-config can leave the chip unusable if it goes wrong");
	if ((rc = ctx_session(x))) return rc;
	if ((rc = read_objs(x, o))) return rc;
	if (cfg_parse(in, o, w, perr, sizeof perr) != 0) return out_error(TC_USAGE, "%s", perr);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) {
		cur[i] = o[i].r_value;
		if (cur[i] != w[i]) changed++;
	}
	if (!changed) {
		out_notice("the chip's R-config already equals %s; nothing to do", in);
		return TC_OK;
	}
	/* guard 1: the key that opened this session must keep the right to read and to rewrite the configuration */
	ew = cfg_find(o, "UAP_R_CONFIG_WRITE_ERASE");
	rd = cfg_find(o, "UAP_R_CONFIG_READ");
	if (!uap_allows(w[ew], UAP_SINGLE, x->key.slot, 0) || !uap_allows(w[rd], UAP_SINGLE, x->key.slot, 0))
		return out_error(TC_REFUSED, "this file would take the right to read or rewrite the configuration away from pairing key %d, the key of this session: the change could never be undone", x->key.slot);
	/* guard 2: objects that are not access rights (start-up, sensors, debug, GPO, sleep) are not touched unless asked */
	if (!restore && !incl)
		for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++)
			if (!is_uap(o[i].name) && cur[i] != w[i])
				return out_error(TC_REFUSED, "the file changes %s, which is not an access right; add --include-non-access if that is intended", o[i].name);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++)
		if (cur[i] != w[i]) {
			printf("%s 0x%08x -> 0x%08x\n", o[i].name, cur[i], w[i]);
			uap_describe_diff(stdout, o[i].name, cur[i], w[i]);
		}
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): checks passed; would save a backup, erase the R-config, write %d changed object(s) and read it back%s", changed, yes ? "" : "; needs --yes-rewrite-r-config");
		return TC_OK;
	}
	if (backup) snprintf(bk, sizeof bk, "%s", backup);
	else {
		stamp(ts, sizeof ts);
		snprintf(bk, sizeof bk, "rconfig-backup-%s.txt", ts);
	}
	if (cfg_write(bk, o, cur, "R-config before tropicctl config rewrite; restore with: tropicctl config restore --in THIS_FILE --yes-rewrite-r-config") != 0)
		return out_error(TC_REFUSED, "cannot save a backup to %s, so the R-config is not touched", bk);
	out_notice("saved the current R-config to %s", bk);
	st = chip_rconfig_rewrite(x->chip, w, &stage, err);
	if (st != CHIP_OK) {
		if (stage == 1)
			return out_error(tc_exit_for_status(st), "the R-config was ERASED but writing it failed (%s). The chip is at its default configuration. Restore with: tropicctl config restore --in %s --yes-rewrite-r-config", err, bk);
		return out_chip_error(st, "the R-config rewrite failed before anything was erased", err);
	}
	st = chip_config_dump(x->chip, after, err);
	if (st != CHIP_OK) return out_error(TC_VERIFY, "the R-config was written but cannot be read back (%s). Backup: %s", err, bk);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++)
		if (after[i].r_value != w[i])
			return out_error(TC_VERIFY, "%s does not read back as written (0x%08x, expected 0x%08x). Restore with: tropicctl config restore --in %s --yes-rewrite-r-config", o[i].name, after[i].r_value, w[i], bk);
	if (g_json) printf("{\"rewritten\":true,\"changed\":%d,\"backup\":\"%s\"}\n", changed, bk);
	else printf("R-config rewritten and read back correctly (%d object(s) changed); backup: %s\n", changed, bk);
	return TC_OK;
}

int cmd_config_rewrite(ctx_t *x, args_t *a) { return do_rewrite(x, a, cmd_find("config", "rewrite"), 0); }
int cmd_config_restore(ctx_t *x, args_t *a) { return do_rewrite(x, a, cmd_find("config", "restore"), 1); }

int cmd_config_burn(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("config", "burn");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT], after[CHIP_CONFIG_OBJ_CNT];
	uint8_t rv[4], sv[4];
	long bit;
	chip_status_t st;
	int rc, i, yes = args_flag(a, "--yes-burn-i-config-bit");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 2 || !args_number(a->argv[1], 0, 31, &bit)) return cmd_usage(c, "an object name and a bit 0 to 31 are needed");
	if (!yes && !x->dry_run) return safety_refuse("--yes-burn-i-config-bit", "an I-config bit only goes from 1 to 0 and can never be set again");
	if ((rc = ctx_session(x))) return rc;
	st = chip_fw_versions(x->chip, rv, sv, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the firmware version", err);
	if (rv[1] < 2)
		return out_error(TC_REFUSED, "the firmware version bytes (%02x %02x %02x %02x) do not show CPU firmware 2.0.0 or newer; older firmware can fail I-Memory writes silently", rv[0], rv[1], rv[2], rv[3]);
	if ((rc = read_objs(x, o))) return rc;
	i = cfg_find(o, a->argv[0]);
	if (i < 0) return out_error(TC_NOTFOUND, "no configuration object named %s", a->argv[0]);
	if (!((o[i].i_value >> bit) & 1u)) {
		out_notice("I-config %s bit %ld is already 0; nothing to do", o[i].name, bit);
		return TC_OK;
	}
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): would clear bit %ld of I-config %s (IRREVERSIBLE)%s", bit, o[i].name, yes ? "" : "; needs --yes-burn-i-config-bit");
		return TC_OK;
	}
	st = chip_iconfig_clear_bit(x->chip, i, (int)bit, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot clear the bit", err);
	st = chip_config_dump(x->chip, after, err);
	if (st != CHIP_OK) return out_error(TC_VERIFY, "the bit was written but the I-config cannot be read back: %s", err);
	if ((after[i].i_value >> bit) & 1u) return out_error(TC_VERIFY, "I-config %s bit %ld is still 1 after the write (I-Memory writes can fail silently)", o[i].name, bit);
	if (g_json) printf("{\"object\":\"%s\",\"bit\":%ld,\"cleared\":true}\n", o[i].name, bit);
	else printf("I-config %s bit %ld cleared (one-way) and read back\n", o[i].name, bit);
	return TC_OK;
}
