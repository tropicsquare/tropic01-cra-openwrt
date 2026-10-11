#include "output.h"

#include <stdarg.h>
#include <string.h>

#include "errors.h"

int g_json, g_quiet, g_verbose, g_legacy;
const char *g_prog = "tropicctl";

void out_notice(const char *fmt, ...)
{
	va_list ap;
	if (g_quiet) return;
	fprintf(stderr, "%s: ", g_prog);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void out_warn(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%s: WARNING: ", g_prog);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void json_str(FILE *f, const char *s)
{
	fputc('"', f);
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
		else if (c == '\n') fputs("\\n", f);
		else if (c == '\r') fputs("\\r", f);
		else if (c == '\t') fputs("\\t", f);
		else if (c < 0x20) fprintf(f, "\\u%04x", c);
		else fputc(c, f);
	}
	fputc('"', f);
}

void hex_to(char *dst, const uint8_t *p, size_t n)
{
	static const char d[] = "0123456789abcdef";
	for (size_t i = 0; i < n; i++) {
		dst[2 * i] = d[p[i] >> 4];
		dst[2 * i + 1] = d[p[i] & 15];
	}
	dst[2 * n] = '\0';
}

void json_hex(FILE *f, const uint8_t *p, size_t n)
{
	fputc('"', f);
	for (size_t i = 0; i < n; i++) fprintf(f, "%02x", p[i]);
	fputc('"', f);
}

int out_error(int code, const char *fmt, ...)
{
	char msg[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	if (g_json) {
		fputs("{\"error\":{\"code\":", stderr);
		fprintf(stderr, "%d,\"name\":", code);
		json_str(stderr, tc_exit_name(code));
		fputs(",\"message\":", stderr);
		json_str(stderr, msg);
		fputs("}}\n", stderr);
	} else if (g_legacy) {
		fprintf(stderr, "%s: ERROR: %s\n", g_prog, msg);
	} else {
		fprintf(stderr, "%s: error: %s\n", g_prog, msg);
	}
	return code;
}

int out_chip_error(chip_status_t st, const char *what, const char *msg)
{
	int code = tc_exit_for_status(st);
	if (g_legacy && !g_json) {
		/* the phrases the old tool printed and that callers look for */
		fprintf(stderr, "ERROR   Error %s: %s\n", st == CHIP_E_SESSION ? "sec channel" : "l3 cmd", msg);
		return code;
	}
	return out_error(code, "%s: %s", what, msg);
}
