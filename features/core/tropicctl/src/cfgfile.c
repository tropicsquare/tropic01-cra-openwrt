#include "cfgfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int cfg_find(const chip_config_obj_t *objs, const char *name)
{
	char full[CHIP_NAME_LEN + 16];
	snprintf(full, sizeof full, "%s%s", strncmp(name, "TR01_CFG_", 9) == 0 ? "" : "TR01_CFG_", name);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++)
		if (strcmp(objs[i].name, full) == 0) return i;
	return -1;
}

int cfg_parse(const char *path, const chip_config_obj_t *objs, uint32_t words[CHIP_CONFIG_OBJ_CNT], char *err, size_t errlen)
{
	FILE *f = fopen(path, "r");
	char line[200];
	int seen[CHIP_CONFIG_OBJ_CNT] = {0}, lineno = 0;
	if (!f) {
		snprintf(err, errlen, "cannot read %s", path);
		return -1;
	}
	while (fgets(line, sizeof line, f)) {
		char a[80], b[80], c[80];
		int n, idx;
		char *end;
		unsigned long v;
		lineno++;
		if (line[0] == '#' || line[strspn(line, " \t\r\n")] == '\0') continue;
		n = sscanf(line, "%79s %79s %79s", a, b, c);
		if (n == 3 && strcmp(a, "I") == 0) continue; /* the one-way objects are not part of a rewrite */
		if (n == 3 && strcmp(a, "R") == 0) {
			memmove(a, b, sizeof b);
			memmove(b, c, sizeof c);
		} else if (n != 2) {
			snprintf(err, errlen, "%s line %d: expected \"R NAME 0xVALUE\"", path, lineno);
			fclose(f);
			return -1;
		}
		idx = cfg_find(objs, a);
		v = strtoul(b, &end, 0);
		if (idx < 0 || *end || strncmp(b, "0x", 2) != 0 || v > 0xfffffffful) {
			snprintf(err, errlen, "%s line %d: unknown object or bad value (%s %s)", path, lineno, a, b);
			fclose(f);
			return -1;
		}
		if (seen[idx]) {
			snprintf(err, errlen, "%s line %d: %s appears twice", path, lineno, a);
			fclose(f);
			return -1;
		}
		seen[idx] = 1;
		words[idx] = (uint32_t)v;
	}
	fclose(f);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++)
		if (!seen[i]) {
			snprintf(err, errlen, "%s does not list %s (every one of the %d R-config objects must be present)", path,
			         objs[i].name, CHIP_CONFIG_OBJ_CNT);
			return -1;
		}
	return 0;
}

int cfg_write(const char *path, const chip_config_obj_t *objs, const uint32_t words[CHIP_CONFIG_OBJ_CNT], const char *comment)
{
	FILE *f;
	if (strcmp(path, "-") == 0) f = stdout;
	else {
		int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		f = fd < 0 ? NULL : fdopen(fd, "w");
	}
	if (!f) return -1;
	if (comment) fprintf(f, "# %s\n", comment);
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) fprintf(f, "R %s 0x%08x\n", objs[i].name, words[i]);
	if (f != stdout) return fclose(f) == 0 ? 0 : -1;
	return fflush(f) == 0 ? 0 : -1;
}
