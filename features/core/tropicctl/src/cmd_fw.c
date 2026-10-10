#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cfgfile.h"
#include "cmd_table.h"
#include "errors.h"
#include "files.h"
#include "output.h"
#include "safety.h"

#define FW_MAX (1024 * 1024)      /* the images are a few tens of kilobytes; refuse anything absurd */
#define MAINT_BIT (1u << 3)       /* BOOTLOADER_CO_CFG_START_UP_MAINTENANCE_ENA_MASK, in both I-config and R-config START_UP */

/* libtropic prints a firmware version as byte 3 . byte 2 . byte 1 */
static void ver_text(char *buf, size_t n, const uint8_t v[4]) { snprintf(buf, n, "%u.%u.%u", v[3], v[2], v[1]); }

/* the three bytes that matter, as a number that compares like a version */
static unsigned ver_num(const uint8_t v[4]) { return ((unsigned)v[3] << 16) | ((unsigned)v[2] << 8) | v[1]; }

/* "vX.Y.Z" or "-vX.Y.Z" in a file name: the libtropic image names are fw_v2.2.0.hex32_signed_chunks.bin and
 * spect_app-v1.3.0_signed_chunks.bin. Returns 1 and sets v (byte order of the chip's version words). */
static int ver_from_name(const char *path, uint8_t v[4])
{
	const char *base = strrchr(path, '/'), *p;
	unsigned a, b, c;
	base = base ? base + 1 : path;
	for (p = base; *p; p++)
		if (*p == 'v' && (p == base || p[-1] == '_' || p[-1] == '-') && sscanf(p + 1, "%u.%u.%u", &a, &b, &c) == 3 && a < 256 && b < 256 && c < 256) {
			v[0] = 0; v[1] = (uint8_t)c; v[2] = (uint8_t)b; v[3] = (uint8_t)a;
			return 1;
		}
	return 0;
}

/* the one file in dir whose name starts with prefix and ends with _signed_chunks.bin */
static int find_image(const char *dir, const char *prefix, char *out, size_t n)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int found = 0;
	if (!d) return -1;
	while ((e = readdir(d))) {
		size_t l = strlen(e->d_name);
		if (strncmp(e->d_name, prefix, strlen(prefix)) == 0 && l > 18 && strcmp(e->d_name + l - 18, "_signed_chunks.bin") == 0) {
			if (found++) { closedir(d); return -2; }
			snprintf(out, n, "%s/%s", dir, e->d_name);
		}
	}
	closedir(d);
	return found == 1 ? 0 : -3;
}

static const char *silicon_of(const chip_id_t *id)
{
	static char s[5];
	/* CHIP_ID v0.0.0.1 has no silicon revision field: those chips are ABAB (libtropic FAQ) */
	if (id->chip_id_ver[0] == 0 && id->chip_id_ver[1] == 0 && id->chip_id_ver[2] == 0 && id->chip_id_ver[3] == 1) return "ABAB";
	memcpy(s, id->silicon_rev, 4);
	s[4] = '\0';
	for (int i = 0; i < 4; i++)
		if (s[i] < 'A' || s[i] > 'Z') return "unknown";
	return s;
}

int cmd_chip_reboot(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("chip", "reboot");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	chip_status_t st;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if (x->dry_run) {
		out_notice("DRY RUN (nothing done): would restart the chip");
		return TC_OK;
	}
	if ((rc = ctx_chip(x))) return rc;
	st = chip_reboot(x->chip, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot restart the chip", err);
	if (g_json) printf("{\"rebooted\":true}\n");
	else printf("chip restarted; the R-config on the chip is now in force\n");
	return TC_OK;
}

int cmd_fw_status(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("fw", "status");
	char err[CHIP_ERRLEN] = "", rv[24], sv[24];
	const char *bad, *si;
	uint8_t r[4], s[4];
	chip_id_t id;
	chip_mode_t m;
	chip_status_t st;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if ((rc = ctx_chip(x))) return rc;
	st = chip_chip_id(x->chip, &id, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read CHIP_ID", err);
	st = chip_fw_versions(x->chip, r, s, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the firmware versions", err);
	st = chip_mode(x->chip, &m, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the chip mode", err);
	ver_text(rv, sizeof rv, r);
	ver_text(sv, sizeof sv, s);
	si = silicon_of(&id);
	if (g_json)
		printf("{\"riscv_version\":\"%s\",\"spect_version\":\"%s\",\"silicon\":\"%s\",\"update_images_for\":\"%s\",\"mode\":\"%s\"}\n", rv, sv, si, chip_update_silicon(),
		       m == CHIP_MODE_APPLICATION ? "application" : m == CHIP_MODE_MAINTENANCE ? "maintenance" : "unknown");
	else {
		printf("RISC-V firmware %s\nSPECT firmware %s\nsilicon revision %s (this build updates %s chips)\nmode %s\n", rv, sv, si, chip_update_silicon(),
		       m == CHIP_MODE_APPLICATION ? "application" : m == CHIP_MODE_MAINTENANCE ? "maintenance" : "unknown");
		if (r[3] < 2) printf("note: CPU firmware older than 2.0.0; I-Memory writes (pairing keys, I-config) can fail silently\n");
		printf("whether the chip can be updated depends on Maintenance Mode in its configuration: tropicctl config get START_UP\n");
	}
	return TC_OK;
}

static void stamp(char *buf, size_t n)
{
	time_t t = time(NULL);
	struct tm tm;
	gmtime_r(&t, &tm);
	strftime(buf, n, "%Y%m%dT%H%M%SZ", &tm);
}

/* set or clear Maintenance Mode in the R-config START_UP object: backup, erase, write, read back, restart the chip */
static int set_maintenance(ctx_t *x, int on, const char *backup)
{
	char err[CHIP_ERRLEN] = "", bk[300], ts[32];
	chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT], after[CHIP_CONFIG_OBJ_CNT];
	uint32_t w[CHIP_CONFIG_OBJ_CNT];
	chip_status_t st;
	int rc, stage = 0;
	if ((rc = ctx_session(x))) return rc;
	st = chip_config_dump(x->chip, o, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the configuration", err);
	if (cfg_find(o, "START_UP") != 0) return out_error(TC_INTERNAL, "unexpected configuration layout");
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) w[i] = o[i].r_value;
	if (on) w[0] |= MAINT_BIT;
	else w[0] &= ~MAINT_BIT;
	if (w[0] == o[0].r_value) return TC_OK;
	if (backup) snprintf(bk, sizeof bk, "%s", backup);
	else {
		stamp(ts, sizeof ts);
		snprintf(bk, sizeof bk, "rconfig-backup-%s.txt", ts);
	}
	{
		uint32_t cur[CHIP_CONFIG_OBJ_CNT];
		for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) cur[i] = o[i].r_value;
		if (cfg_write(bk, o, cur, "R-config before tropicctl fw update changed Maintenance Mode; restore with: tropicctl config restore --in THIS_FILE --yes-rewrite-r-config") != 0)
			return out_error(TC_REFUSED, "cannot save a backup to %s, so the R-config is not touched", bk);
	}
	out_notice("saved the current R-config to %s", bk);
	st = chip_rconfig_rewrite(x->chip, w, &stage, err);
	if (st != CHIP_OK) {
		if (stage == 1)
			return out_error(tc_exit_for_status(st), "the R-config was ERASED but writing it failed (%s). Restore with: tropicctl config restore --in %s --yes-rewrite-r-config", err, bk);
		return out_chip_error(st, "the R-config rewrite failed before anything was erased", err);
	}
	st = chip_config_dump(x->chip, after, err);
	if (st != CHIP_OK) return out_error(TC_VERIFY, "the R-config was written but cannot be read back (%s). Backup: %s", err, bk);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++)
		if (after[i].r_value != w[i])
			return out_error(TC_VERIFY, "%s does not read back as written. Restore with: tropicctl config restore --in %s --yes-rewrite-r-config", o[i].name, bk);
	st = chip_reboot(x->chip, err);
	x->have_session = 0;
	if (st != CHIP_OK) return out_error(tc_exit_for_status(st), "Maintenance Mode was changed in the R-config but the chip could not be restarted (%s); it takes effect at the next restart or power cycle", err);
	out_notice("Maintenance Mode %s in the R-config and the chip restarted", on ? "enabled" : "disabled");
	return TC_OK;
}

int cmd_fw_update(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("fw", "update");
	char err[CHIP_ERRLEN] = "", cpuf[400], spf[400], cv[24], sv[24], tcv[24], tsv[24];
	const char *dir = NULL, *cpu_path = NULL, *sp_path = NULL, *backup = NULL, *bad, *si;
	static uint8_t cpu[FW_MAX], spect[FW_MAX];
	size_t cpulen = 0, splen = 0;
	uint8_t cur_r[4], cur_s[4], new_r[4], new_s[4], chk_r[4], chk_s[4];
	chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT];
	chip_id_t *id;
	chip_status_t st;
	int rc, enable = 0, disable = 0, yes = args_flag(a, "--yes-update-firmware");
	enable = args_flag(a, "--enable-maintenance");
	disable = args_flag(a, "--disable-maintenance");
	if (args_value(a, "--dir", &dir) < 0 || args_value(a, "--cpu", &cpu_path) < 0 || args_value(a, "--spect", &sp_path) < 0 ||
	    args_value(a, "--backup", &backup) < 0)
		return cmd_usage(c, "an option needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if (dir && (cpu_path || sp_path)) return cmd_usage(c, "give --dir, or --cpu and --spect, not both");
	if (dir) {
		int r1 = find_image(dir, "fw_v", cpuf, sizeof cpuf), r2 = find_image(dir, "spect_app-v", spf, sizeof spf);
		if (r1 || r2) return out_error(TC_USAGE, "%s must hold exactly one fw_v*_signed_chunks.bin and one spect_app-v*_signed_chunks.bin", dir);
		cpu_path = cpuf;
		sp_path = spf;
	}
	if (!cpu_path || !sp_path) return cmd_usage(c, "--dir, or --cpu and --spect, is needed");
	if (!ver_from_name(cpu_path, new_r) || !ver_from_name(sp_path, new_s))
		return out_error(TC_USAGE, "cannot read the version from the image file names (expected ...v2.2.0... and ...v1.3.0...); use the files as libtropic ships them");
	if (file_read(cpu_path, cpu, sizeof cpu, &cpulen) != 0 || cpulen == 0) return out_error(TC_USAGE, "cannot read %s (or it is larger than %d bytes)", cpu_path, FW_MAX);
	if (file_read(sp_path, spect, sizeof spect, &splen) != 0 || splen == 0) return out_error(TC_USAGE, "cannot read %s (or it is larger than %d bytes)", sp_path, FW_MAX);
	if (!yes && !x->dry_run) return safety_refuse("--yes-update-firmware", "a firmware update cannot be undone (the chip never accepts an older version) and a failed one can leave the chip unusable");
	if ((rc = ctx_chip(x))) return rc;
	/* the images are different for each silicon revision: the wrong ones are a way to ruin a chip */
	id = calloc(1, sizeof *id);
	if (!id) return out_error(TC_INTERNAL, "out of memory");
	st = chip_chip_id(x->chip, id, err);
	if (st != CHIP_OK) {
		free(id);
		return out_chip_error(st, "cannot read CHIP_ID", err);
	}
	si = silicon_of(id);
	if (strcmp(si, chip_update_silicon()) != 0) {
		rc = out_error(TC_REFUSED, "this chip is silicon revision %s but this build of tropicctl updates %s chips; use a build for the right revision (libtropic option LT_SILICON_REV) and its images", si, chip_update_silicon());
		free(id);
		return rc;
	}
	free(id);
	/* a restart first: after a restart the chip runs its application firmware, which is the only state in which the versions can be read */
	if (!x->dry_run) {
		st = chip_reboot(x->chip, err);
		if (st != CHIP_OK) return out_chip_error(st, "cannot restart the chip", err);
	}
	st = chip_fw_versions(x->chip, cur_r, cur_s, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the firmware versions", err);
	ver_text(cv, sizeof cv, cur_r); ver_text(sv, sizeof sv, cur_s);
	ver_text(tcv, sizeof tcv, new_r); ver_text(tsv, sizeof tsv, new_s);
	out_notice("chip: RISC-V %s, SPECT %s; images: RISC-V %s, SPECT %s", cv, sv, tcv, tsv);
	if (ver_num(new_r) < ver_num(cur_r) || ver_num(new_s) < ver_num(cur_s))
		return out_error(TC_REFUSED, "an image is older than what the chip runs; the chip does not accept a downgrade");
	if (ver_num(new_r) == ver_num(cur_r) && ver_num(new_s) == ver_num(cur_s)) {
		out_notice("the chip already runs these versions; nothing to do");
		return TC_OK;
	}
	/* Maintenance Mode: the update helper restarts the chip into it. The I-config bit cannot be set again by anyone. */
	if ((rc = ctx_session(x))) return rc;
	st = chip_config_dump(x->chip, o, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the configuration", err);
	if (!(o[0].i_value & MAINT_BIT))
		return out_error(TC_REFUSED, "Maintenance Mode is disabled in the I-config (irreversible), so this chip cannot be updated");
	if (!(o[0].r_value & MAINT_BIT) && !enable)
		return out_error(TC_REFUSED, "Maintenance Mode is disabled in the R-config; add --enable-maintenance to enable it for the update (R-config is rewritten with a backup)");
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): checks passed; would %supdate the firmware%s%s", !(o[0].r_value & MAINT_BIT) ? "enable Maintenance Mode, " : "", disable ? ", then disable Maintenance Mode" : "", yes ? "" : "; needs --yes-update-firmware");
		return TC_OK;
	}
	if (!(o[0].r_value & MAINT_BIT) && (rc = set_maintenance(x, 1, backup))) return rc;
	chip_session_end(x->chip, err); /* the helper needs the session closed; it restarts the chip */
	x->have_session = 0;
	out_notice("updating the firmware; do not interrupt and do not remove power");
	st = chip_fw_update(x->chip, cpu, cpulen, new_r, spect, splen, new_s, err);
	if (st != CHIP_OK)
		return out_error(tc_exit_for_status(st), "the firmware update failed: %s. At least one bank may still hold working firmware: run the update again until it succeeds. Maintenance Mode may have been left enabled in the R-config", err);
	st = chip_reboot(x->chip, err);
	if (st != CHIP_OK) return out_chip_error(st, "the update reported success but the chip cannot be restarted", err);
	st = chip_fw_versions(x->chip, chk_r, chk_s, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the firmware versions after the update", err);
	if (ver_num(chk_r) != ver_num(new_r) || ver_num(chk_s) != ver_num(new_s)) {
		ver_text(cv, sizeof cv, chk_r); ver_text(sv, sizeof sv, chk_s);
		return out_error(TC_VERIFY, "the update reported success but the chip runs RISC-V %s, SPECT %s, not %s and %s", cv, sv, tcv, tsv);
	}
	if (disable && (rc = set_maintenance(x, 0, NULL))) return rc;
	if (g_json) printf("{\"updated\":true,\"riscv_version\":\"%s\",\"spect_version\":\"%s\",\"maintenance_mode_left_enabled\":%s}\n", tcv, tsv, disable ? "false" : "true");
	else {
		printf("firmware updated and read back: RISC-V %s, SPECT %s\n", tcv, tsv);
		if (!disable) printf("Maintenance Mode is still enabled in the R-config; disable it to reduce the attack surface: tropicctl fw maintenance off --yes-rewrite-r-config\n");
	}
	return TC_OK;
}

int cmd_fw_maintenance(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("fw", "maintenance");
	const char *bad, *backup = NULL;
	int yes = args_flag(a, "--yes-rewrite-r-config"), on;
	if (args_value(a, "--backup", &backup) < 0) return cmd_usage(c, "--backup needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 1 || (strcmp(a->argv[0], "on") != 0 && strcmp(a->argv[0], "off") != 0)) return cmd_usage(c, "say on or off");
	on = strcmp(a->argv[0], "on") == 0;
	if (!yes && !x->dry_run) return safety_refuse("--yes-rewrite-r-config", "this rewrites the R-config, which can leave the chip unusable if it goes wrong");
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): would turn Maintenance Mode %s in the R-config (backup, erase, write, read back, restart)", on ? "on" : "off");
		return TC_OK;
	}
	return set_maintenance(x, on, backup);
}
