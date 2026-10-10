/*
 * output.h -- messages and result writers.
 *
 * Results go to stdout; notices, warnings and errors go to stderr. With --json an error is a JSON object on stderr.
 * In legacy mode (started as `lt-util`) messages keep the texts the old tool printed, because callers parse them.
 */
#ifndef TROPICCTL_OUTPUT_H
#define TROPICCTL_OUTPUT_H

#include <stdint.h>
#include <stdio.h>

#include "chip.h"

extern int g_json, g_quiet, g_verbose, g_legacy;
extern const char *g_prog;

void out_notice(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void out_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* print an error and return code */
int out_error(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* print a chip error (legacy mode adds the old "Error sec channel" / "Error l3 cmd" phrases) and return its exit code */
int out_chip_error(chip_status_t st, const char *what, const char *msg);

void json_str(FILE *f, const char *s);
void json_hex(FILE *f, const uint8_t *p, size_t n);
void hex_to(char *dst, const uint8_t *p, size_t n); /* dst needs 2n+1 bytes */

#endif
