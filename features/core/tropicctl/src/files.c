#include "files.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int file_read(const char *path, uint8_t *buf, size_t cap, size_t *len)
{
	FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
	size_t n;
	if (!f) return -1;
	n = fread(buf, 1, cap, f);
	if (n == cap && fgetc(f) != EOF) {
		if (f != stdin) fclose(f);
		return -2;
	}
	if (ferror(f)) {
		if (f != stdin) fclose(f);
		return -1;
	}
	if (f != stdin) fclose(f);
	*len = n;
	return 0;
}

int file_write(const char *path, const uint8_t *buf, size_t len)
{
	if (strcmp(path, "-") == 0) {
		if (fwrite(buf, 1, len, stdout) != len) return -1;
		return fflush(stdout) == 0 ? 0 : -1;
	}
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return -1;
	size_t off = 0;
	while (off < len) {
		ssize_t w = write(fd, buf + off, len - off);
		if (w <= 0) {
			close(fd);
			return -1;
		}
		off += (size_t)w;
	}
	return close(fd) == 0 ? 0 : -1;
}
