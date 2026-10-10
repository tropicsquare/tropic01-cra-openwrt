/*
 * tropicctl -- command-line tool for the TROPIC01 secure element. See ../SPEC.md and `tropicctl help`.
 * Started under the name `lt-util` it runs in legacy mode (legacy.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "cmd_table.h"
#include "errors.h"
#include "output.h"

int legacy_main(int argc, char **argv, ctx_t *x);

static const char *base_name(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

int main(int argc, char **argv)
{
	ctx_t x;
	const char *argv0 = getenv("TROPICCTL_ARGV0");
	int i = 1, rc;
	const cmd_t *c;
	args_t rest;

	ctx_init(&x);
	if (!argv0 || !*argv0) argv0 = argv[0];
	if (strcmp(base_name(argv0), "lt-util") == 0) {
		g_legacy = 1;
		g_prog = "lt-util";
		rc = legacy_main(argc, argv, &x);
		ctx_close(&x);
		return rc;
	}

	/* global options come before the area */
	for (; i < argc && argv[i][0] == '-'; i++) {
		const char *o = argv[i];
		long v;
		if (strcmp(o, "--json") == 0) g_json = 1;
		else if (strcmp(o, "-q") == 0) g_quiet = 1;
		else if (strcmp(o, "-v") == 0) g_verbose = 1;
		else if (strcmp(o, "--dry-run") == 0) x.dry_run = 1;
		else if (strcmp(o, "--no-lock") == 0) x.no_lock = 1;
		else if (strcmp(o, "--version") == 0) {
			print_version();
			return TC_OK;
		} else if (strcmp(o, "-h") == 0 || strcmp(o, "--help") == 0) return cmd_help(0, NULL);
		else if (strcmp(o, "--spi-dev") == 0 || strcmp(o, "--gpio-dev") == 0 || strcmp(o, "--cs-pin") == 0 ||
		         strcmp(o, "--spi-speed") == 0 || strcmp(o, "--key") == 0 || strcmp(o, "--lock-timeout") == 0) {
			if (i + 1 >= argc) return out_error(TC_USAGE, "%s needs a value", o);
			const char *val = argv[++i];
			if (strcmp(o, "--spi-dev") == 0) snprintf(x.cfg.spi_dev, sizeof x.cfg.spi_dev, "%s", val);
			else if (strcmp(o, "--gpio-dev") == 0) snprintf(x.cfg.gpio_dev, sizeof x.cfg.gpio_dev, "%s", val);
			else if (strcmp(o, "--key") == 0) x.key_source = val;
			else if (!args_number(val, 0, 1000000000L, &v)) return out_error(TC_USAGE, "%s needs a number", o);
			else if (strcmp(o, "--cs-pin") == 0) x.cfg.cs_pin = (int)v;
			else if (strcmp(o, "--spi-speed") == 0) x.cfg.speed = (int)v;
			else x.lock_timeout = (int)v;
		} else return out_error(TC_USAGE, "unknown global option %s (see: tropicctl help)", o);
	}
	if (i >= argc) return cmd_help(0, NULL);
	if (strcmp(argv[i], "help") == 0) return cmd_help(argc - i - 1, argv + i + 1);
	if (strcmp(argv[i], "version") == 0) {
		print_version();
		return TC_OK;
	}
	if (i + 1 >= argc) return out_error(TC_USAGE, "'%s' needs a command (see: tropicctl help %s)", argv[i], argv[i]);
	c = cmd_find(argv[i], argv[i + 1]);
	if (!c) return out_error(TC_USAGE, "unknown command '%s %s' (see: tropicctl help)", argv[i], argv[i + 1]);
	rest.argc = argc - i - 2;
	rest.argv = argv + i + 2;
	rc = c->fn(&x, &rest);
	ctx_close(&x);
	return rc;
}
