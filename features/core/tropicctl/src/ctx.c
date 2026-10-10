#include "ctx.h"

#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "keys.h"
#include "lock.h"
#include "output.h"

void ctx_init(ctx_t *x)
{
	const char *e;
	memset(x, 0, sizeof *x);
	snprintf(x->cfg.spi_dev, sizeof x->cfg.spi_dev, "%s", (e = getenv("TROPICCTL_SPI_DEV")) && *e ? e : TROPICCTL_SPI_DEV);
	snprintf(x->cfg.gpio_dev, sizeof x->cfg.gpio_dev, "%s", (e = getenv("TROPICCTL_GPIO_DEV")) && *e ? e : TROPICCTL_GPIO_DEV);
	x->cfg.cs_pin = (e = getenv("TROPICCTL_CS_PIN")) && *e ? atoi(e) : TROPICCTL_CS_PIN;
	x->cfg.speed = (e = getenv("TROPICCTL_SPI_SPEED")) && *e ? atoi(e) : TROPICCTL_SPI_SPEED;
	x->lock_timeout = 10;
}

int ctx_chip(ctx_t *x)
{
	char err[CHIP_ERRLEN] = "";
	chip_status_t st;
	if (x->chip) return TC_OK;
	if (!x->no_lock) {
		int rc = lock_acquire(x->lock_timeout);
		if (rc) return rc;
	}
	st = chip_open(&x->cfg, &x->chip, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot open the chip", err);
	return TC_OK;
}

int ctx_session(ctx_t *x)
{
	char err[CHIP_ERRLEN] = "";
	chip_status_t st;
	int rc = ctx_chip(x);
	if (rc) return rc;
	if (x->have_session) return TC_OK;
	rc = keys_select(x->key_source, &x->key, x->key_desc, sizeof x->key_desc);
	if (rc) return rc;
	st = chip_session(x->chip, &x->key, err);
	if (st != CHIP_OK) return out_chip_error(st, "secure session failed", err);
	x->have_session = 1;
	return TC_OK;
}

void ctx_close(ctx_t *x)
{
	if (x->chip) chip_close(x->chip);
	x->chip = NULL;
	memset(&x->key, 0, sizeof x->key); /* the private half must not linger */
}
