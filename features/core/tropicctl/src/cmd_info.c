#include <stdio.h>

#include "cmd_table.h"
#include "errors.h"
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

int cmd_info_fw(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("info", "fw");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	uint8_t r[4], s[4];
	chip_status_t st;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = ctx_chip(x))) return rc;
	st = chip_fw_versions(x->chip, r, s, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the firmware versions", err);
	if (g_json) {
		printf("{\"riscv_fw\":");
		json_hex(stdout, r, 4);
		printf(",\"spect_fw\":");
		json_hex(stdout, s, 4);
		printf("}\n");
	} else {
		printf("RISCV_FW_VER %02x %02x %02x %02x\nSPECT_FW_VER %02x %02x %02x %02x\n", r[0], r[1], r[2], r[3], s[0], s[1],
		       s[2], s[3]);
	}
	return TC_OK;
}
