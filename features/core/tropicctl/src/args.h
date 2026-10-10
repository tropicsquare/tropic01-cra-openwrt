/* args.h -- tiny option extraction. A handler takes the options it knows; anything starting with "--" left over is a
 * usage error, so a misspelt option is never silently ignored. */
#ifndef TROPICCTL_ARGS_H
#define TROPICCTL_ARGS_H

typedef struct {
	int argc;
	char **argv;
} args_t;

int args_flag(args_t *a, const char *name);                       /* 1 if present (removed) */
int args_value(args_t *a, const char *name, const char **out);    /* 1 found, 0 absent, -1 value missing */
int args_unknown(const args_t *a, const char **which);            /* 1 if an unconsumed "--x" is left */
int args_number(const char *s, long min, long max, long *out);    /* strict decimal parse, 1 ok */

#endif
