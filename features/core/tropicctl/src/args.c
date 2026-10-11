#include "args.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void drop(args_t *a, int i, int n)
{
	for (int k = i; k + n < a->argc; k++) a->argv[k] = a->argv[k + n];
	a->argc -= n;
}

int args_flag(args_t *a, const char *name)
{
	for (int i = 0; i < a->argc; i++)
		if (strcmp(a->argv[i], name) == 0) {
			drop(a, i, 1);
			return 1;
		}
	return 0;
}

int args_value(args_t *a, const char *name, const char **out)
{
	for (int i = 0; i < a->argc; i++)
		if (strcmp(a->argv[i], name) == 0) {
			if (i + 1 >= a->argc) return -1;
			*out = a->argv[i + 1];
			drop(a, i, 2);
			return 1;
		}
	return 0;
}

int args_unknown(const args_t *a, const char **which)
{
	for (int i = 0; i < a->argc; i++)
		if (a->argv[i][0] == '-' && a->argv[i][1] == '-') {
			*which = a->argv[i];
			return 1;
		}
	return 0;
}

int args_number(const char *s, long min, long max, long *out)
{
	char *end;
	long v;
	if (!s || !*s) return 0;
	errno = 0;
	v = strtol(s, &end, 10);
	if (errno || *end || v < min || v > max) return 0;
	*out = v;
	return 1;
}
