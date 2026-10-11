/* ctx.h -- what a command needs: the global options and a way to reach the chip. */
#ifndef TROPICCTL_CTX_H
#define TROPICCTL_CTX_H

#include "chip.h"

typedef struct {
	chip_cfg_t cfg;
	const char *key_source; /* NULL = auto */
	int dry_run, no_lock, lock_timeout;
	chip_t *chip;
	pairing_key_t key;
	char key_desc[96];
	int have_session;
	const char *pin_file; /* --pin-file */
	int pin_stdin;        /* --pin-stdin */
} ctx_t;

/* defaults from the build and from TROPICCTL_SPI_DEV, TROPICCTL_GPIO_DEV, TROPICCTL_CS_PIN, TROPICCTL_SPI_SPEED */
void ctx_init(ctx_t *x);
/* take the lock and open the chip (no session). Returns 0 or a TC_* code; the message has been printed. */
int ctx_chip(ctx_t *x);
/* ctx_chip, then choose the pairing key and start the secure session */
int ctx_session(ctx_t *x);
void ctx_close(ctx_t *x);

#endif
