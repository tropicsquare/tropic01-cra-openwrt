/* pin.h -- reading a PIN without putting it on the command line. */
#ifndef TROPICCTL_PIN_H
#define TROPICCTL_PIN_H

#include <stddef.h>

/* From the file, from stdin, or by prompting on the terminal (no echo). Returns 0, or a TC_* code after printing the
 * message. The PIN must be 4 to 8 characters. */
int pin_read(const char *pin_file, int from_stdin, const char *prompt, char *pin, size_t cap);

#endif
