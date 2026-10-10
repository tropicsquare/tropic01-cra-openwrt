/*
 * cmd_table.h -- the command table. It is the single source of truth: dispatch, `help`, the generated reference and
 * the manual page all read it, so the documentation cannot disagree with the code.
 */
#ifndef TROPICCTL_CMD_TABLE_H
#define TROPICCTL_CMD_TABLE_H

#include <stddef.h>

#include "args.h"
#include "ctx.h"

typedef int (*cmd_fn)(ctx_t *x, args_t *a);

typedef struct {
	const char *area, *verb;
	char cls;             /* safety class: R read-only, W reversible write, I irreversible, H hazardous */
	const char *usage;    /* arguments and command options */
	const char *summary;  /* one line */
	const char *details;  /* a short paragraph for the reference */
	cmd_fn fn;
} cmd_t;

extern const cmd_t g_cmds[];
extern const size_t g_ncmds;

const cmd_t *cmd_find(const char *area, const char *verb);
int cmd_usage(const cmd_t *c, const char *why); /* prints the usage error, returns TC_USAGE */

/* handlers */
int cmd_info_chip(ctx_t *x, args_t *a);
int cmd_info_fw(ctx_t *x, args_t *a);
int cmd_rng_get(ctx_t *x, args_t *a);
int cmd_mem_list(ctx_t *x, args_t *a);
int cmd_mem_read(ctx_t *x, args_t *a);
int cmd_mem_write(ctx_t *x, args_t *a);
int cmd_mem_erase(ctx_t *x, args_t *a);
int cmd_pairing_list(ctx_t *x, args_t *a);
int cmd_pairing_read(ctx_t *x, args_t *a);
int cmd_config_dump(ctx_t *x, args_t *a);
int cmd_session_test(ctx_t *x, args_t *a);

/* help, version */
int cmd_help(int argc, char **argv);
void print_version(void);

#endif
