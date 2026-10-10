#include <stdio.h>

#include "cmd_table.h"
#include "errors.h"
#include "output.h"

int cmd_config_dump(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("config", "dump");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	chip_config_obj_t o[CHIP_CONFIG_OBJ_CNT];
	chip_status_t st;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if ((rc = ctx_session(x))) return rc;
	st = chip_config_dump(x->chip, o, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the configuration", err);
	if (g_json) {
		printf("{\"r_config\":[");
		for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) printf("%s{\"name\":\"%s\",\"value\":\"0x%08x\"}", i ? "," : "", o[i].name, o[i].r_value);
		printf("],\"i_config\":[");
		for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) printf("%s{\"name\":\"%s\",\"value\":\"0x%08x\"}", i ? "," : "", o[i].name, o[i].i_value);
		printf("]}\n");
		return TC_OK;
	}
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) printf("R %s 0x%08x\n", o[i].name, o[i].r_value);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) printf("I %s 0x%08x\n", o[i].name, o[i].i_value);
	return TC_OK;
}
