#include "pin.h"

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "errors.h"
#include "mnd.h"
#include "output.h"

static int from_stream(FILE *f, char *pin, size_t cap)
{
	if (!fgets(pin, (int)cap, f)) return -1;
	pin[strcspn(pin, "\r\n")] = '\0';
	return 0;
}

int pin_read(const char *pin_file, int from_stdin, const char *prompt, char *pin, size_t cap)
{
	pin[0] = '\0';
	if (pin_file) {
		FILE *f = fopen(pin_file, "r");
		if (!f || from_stream(f, pin, cap) != 0) {
			if (f) fclose(f);
			return out_error(TC_USAGE, "cannot read the PIN from %s", pin_file);
		}
		fclose(f);
	} else if (from_stdin) {
		if (from_stream(stdin, pin, cap) != 0) return out_error(TC_USAGE, "no PIN on standard input");
	} else if (isatty(0)) {
		struct termios t, q;
		int have = tcgetattr(0, &t) == 0;
		fprintf(stderr, "%s", prompt);
		if (have) {
			q = t;
			q.c_lflag &= ~(tcflag_t)ECHO;
			tcsetattr(0, TCSAFLUSH, &q);
		}
		if (from_stream(stdin, pin, cap) != 0) pin[0] = '\0';
		if (have) tcsetattr(0, TCSAFLUSH, &t);
		fputc('\n', stderr);
	} else {
		return out_error(TC_USAGE, "a PIN is needed: use --pin-file FILE, --pin-stdin, or run on a terminal");
	}
	if (strlen(pin) < MND_PIN_MIN || strlen(pin) > MND_PIN_MAX)
		return out_error(TC_USAGE, "the PIN must be %d to %d characters", MND_PIN_MIN, MND_PIN_MAX);
	return TC_OK;
}
