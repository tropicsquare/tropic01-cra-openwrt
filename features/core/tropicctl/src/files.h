/* files.h -- reading and writing whole files; "-" means stdin / stdout. Created files are mode 0600. */
#ifndef TROPICCTL_FILES_H
#define TROPICCTL_FILES_H

#include <stddef.h>
#include <stdint.h>

/* reads up to cap bytes; returns 0 and sets *len, -1 on error, -2 if the input is longer than cap */
int file_read(const char *path, uint8_t *buf, size_t cap, size_t *len);
int file_write(const char *path, const uint8_t *buf, size_t len);

#endif
