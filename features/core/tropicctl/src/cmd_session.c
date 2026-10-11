#include <stdio.h>

#include "cmd_table.h"
#include "errors.h"
#include "output.h"

int cmd_session_test(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("session", "test");
	const char *bad;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = ctx_session(x))) return rc;
	if (g_json) {
		printf("{\"session\":\"ok\",\"slot\":%d,\"key\":", x->key.slot);
		json_str(stdout, x->key_desc);
		printf("}\n");
	} else {
		printf("session opened with the %s\n", x->key_desc);
	}
	return TC_OK;
}
