#include <stdio.h>

#include "cmd_table.h"
#include "errors.h"
#include "output.h"
#include "safety.h"

static int n_arg(const cmd_t *c, const char *s, unsigned *n)
{
	long v;
	if (!args_number(s, 0, CHIP_MCOUNTERS - 1, &v)) return cmd_usage(c, "the counter is 0 to 15");
	*n = (unsigned)v;
	return TC_OK;
}

int cmd_counter_get(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("counter", "get");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	uint32_t v;
	unsigned n;
	chip_status_t st;
	int rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 1) return cmd_usage(c, "one counter number is needed");
	if ((rc = n_arg(c, a->argv[0], &n))) return rc;
	if ((rc = ctx_session(x))) return rc;
	st = chip_counter_get(x->chip, n, &v, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the counter", err);
	if (g_json) printf("{\"counter\":%u,\"value\":%u}\n", n, v);
	else printf("counter %u: %u\n", n, v);
	return TC_OK;
}

int cmd_counter_init(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("counter", "init");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	uint32_t v;
	long val;
	unsigned n;
	chip_status_t st;
	int rc, yes = args_flag(a, "--yes-init-counter");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 2) return cmd_usage(c, "a counter number and a value are needed");
	if ((rc = n_arg(c, a->argv[0], &n))) return rc;
	if (!args_number(a->argv[1], 0, CHIP_MCOUNTER_MAX, &val)) return cmd_usage(c, "the value is 0 to 4294967294");
	if (!yes && !x->dry_run) return safety_refuse("--yes-init-counter", "initialising a counter sets the value a rollback check relies on");
	if ((rc = ctx_session(x))) return rc;
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): would set counter %u to %ld%s", n, val, yes ? "" : "; needs --yes-init-counter");
		return TC_OK;
	}
	st = chip_counter_init(x->chip, n, (uint32_t)val, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot initialise the counter", err);
	st = chip_counter_get(x->chip, n, &v, err);
	if (st != CHIP_OK || v != (uint32_t)val) return out_error(TC_VERIFY, "counter %u does not read back as %ld", n, val);
	if (g_json) printf("{\"counter\":%u,\"value\":%u,\"verified\":true}\n", n, v);
	else printf("counter %u: set to %u and read back\n", n, v);
	return TC_OK;
}

int cmd_counter_update(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("counter", "update");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	uint32_t before, after;
	unsigned n;
	chip_status_t st;
	int rc, yes = args_flag(a, "--yes-update-counter");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 1) return cmd_usage(c, "one counter number is needed");
	if ((rc = n_arg(c, a->argv[0], &n))) return rc;
	if (!yes && !x->dry_run) return safety_refuse("--yes-update-counter", "a counter update decrements the counter by one");
	if ((rc = ctx_session(x))) return rc;
	st = chip_counter_get(x->chip, n, &before, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the counter first", err);
	if (before == 0) return out_error(TC_REFUSED, "counter %u is already 0 and cannot be decremented", n);
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): counter %u is %u; an update would make it %u%s", n, before, before - 1, yes ? "" : "; needs --yes-update-counter");
		return TC_OK;
	}
	st = chip_counter_update(x->chip, n, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot update the counter", err);
	st = chip_counter_get(x->chip, n, &after, err);
	if (st != CHIP_OK || after != before - 1) return out_error(TC_VERIFY, "counter %u is %u after the update; expected %u", n, after, before - 1);
	if (g_json) printf("{\"counter\":%u,\"before\":%u,\"after\":%u}\n", n, before, after);
	else printf("counter %u: %u -> %u\n", n, before, after);
	return TC_OK;
}
