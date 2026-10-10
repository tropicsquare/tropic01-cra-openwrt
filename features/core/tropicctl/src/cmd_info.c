#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "cmd_table.h"
#include "errors.h"
#include "files.h"
#include "output.h"

int cmd_info_chip(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("info", "chip");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	chip_id_t id;
	chip_status_t st;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = ctx_chip(x))) return rc;
	st = chip_chip_id(x->chip, &id, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read CHIP_ID", err);
	if (!g_json) {
		fputs(id.text, stdout);
		return TC_OK;
	}
#define G(f) do { printf("\"" #f "\":"); json_hex(stdout, id.f, sizeof id.f); printf(","); } while (0)
	printf("{");
	G(chip_id_ver); G(fl_chip_info); G(func_test_info); G(silicon_rev); G(packg_type_id); G(prov_ver_fab_id_pn);
	G(provisioning_date); G(hsm_ver); G(prog_ver); G(part_num_data); G(prov_templ_ver); G(prov_templ_tag);
	G(prov_spec_ver); G(prov_spec_tag); G(batch_id);
#undef G
	printf("\"text\":");
	json_str(stdout, id.text);
	printf("}\n");
	return TC_OK;
}

/* libtropic prints a firmware version as byte 3 . byte 2 . byte 1 (its fw_update example); byte 0 is not part of it */
static void fw_version(char *buf, size_t n, const uint8_t v[4]) { snprintf(buf, n, "%u.%u.%u", v[3], v[2], v[1]); }

int cmd_info_fw(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("info", "fw");
	char err[CHIP_ERRLEN] = "", rver[24], sver[24];
	const char *bad;
	uint8_t r[4], s[4];
	chip_status_t st;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = ctx_chip(x))) return rc;
	st = chip_fw_versions(x->chip, r, s, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the firmware versions", err);
	fw_version(rver, sizeof rver, r);
	fw_version(sver, sizeof sver, s);
	if (g_json) {
		printf("{\"riscv_fw\":");
		json_hex(stdout, r, 4);
		printf(",\"spect_fw\":");
		json_hex(stdout, s, 4);
		printf(",\"riscv_version\":\"%s\",\"spect_version\":\"%s\"}\n", rver, sver);
	} else {
		printf("RISCV_FW_VER %02x %02x %02x %02x\nSPECT_FW_VER %02x %02x %02x %02x\n", r[0], r[1], r[2], r[3], s[0], s[1], s[2], s[3]);
		if (!g_legacy) printf("RISC-V firmware %s, SPECT firmware %s\n", rver, sver); /* legacy output stays as it was */
	}
	return TC_OK;
}

int cmd_info_mode(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("info", "mode");
	char err[CHIP_ERRLEN] = "";
	const char *bad, *name;
	chip_mode_t m;
	chip_status_t st;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = ctx_chip(x))) return rc;
	st = chip_mode(x->chip, &m, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the chip mode", err);
	name = m == CHIP_MODE_APPLICATION ? "application" : m == CHIP_MODE_MAINTENANCE ? "maintenance" : "unknown";
	if (g_json) printf("{\"mode\":\"%s\"}\n", name);
	else printf("%s\n", name);
	return TC_OK;
}

int cmd_info_cert(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("info", "cert");
	char err[CHIP_ERRLEN] = "", path[400];
	const char *dir = NULL, *bad;
	static uint8_t certs[CHIP_CERTS][CHIP_CERT_MAX];
	size_t len[CHIP_CERTS];
	chip_status_t st;
	int rc;
	if (args_value(a, "--out-dir", &dir) < 0) return cmd_usage(c, "--out-dir needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = ctx_chip(x))) return rc;
	st = chip_certs(x->chip, certs, len, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the certificate store", err);
	if (dir) mkdir(dir, 0700);
	if (g_json) printf("{\"certificates\":[");
	for (int i = 0; i < CHIP_CERTS; i++) {
		if (dir && len[i]) {
			snprintf(path, sizeof path, "%s/cert%d.der", dir, i);
			if (file_write(path, certs[i], len[i]) != 0) return out_error(TC_INTERNAL, "cannot write %s", path);
		}
		if (g_json) printf("%s{\"index\":%d,\"bytes\":%zu}", i ? "," : "", i, len[i]);
		else printf("certificate %d: %zu bytes\n", i, len[i]);
	}
	if (g_json) printf("]}\n");
	return TC_OK;
}
