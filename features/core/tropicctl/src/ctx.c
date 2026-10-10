#include "ctx.h"

#include <stdio.h>
#include <unistd.h>

#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "errors.h"
#include "files.h"
#include "args.h"
#include "keys.h"
#include "lock.h"
#include "mnd.h"
#include "output.h"
#include "pin.h"

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

/*
 * --key pin (SPEC.md section 5, tier 1 of the cra-04 design): open a bootstrap session with the per-unit key, run the
 * MAC-and-Destroy PIN check, decrypt the protected pairing key with the released key, end the bootstrap session and
 * open the real one. Files in the key directory: pin.state (the PIN scheme's host state), protected.enc (the protected
 * private key, sealed under the released key), protected.slot (its pairing slot), and optionally pin.add (additional
 * data mixed into the PIN).
 */
static int session_pin(ctx_t *x)
{
	char err[CHIP_ERRLEN] = "", path[400], pin[64];
	uint8_t k[32], priv[32], add[MND_ADD_MAX], enc[64];
	size_t addlen = 0, enclen = 0;
	pairing_key_t boot, prot;
	mnd_state_t *ms;
	chip_status_t st, cst = CHIP_OK;
	mnd_result_t mr;
	long slot;
	int rc;
	char desc[96];
	snprintf(path, sizeof path, "%s/protected.slot", keys_dir());
	{
		FILE *f = fopen(path, "r");
		char buf[16] = {0};
		if (!f || !fgets(buf, sizeof buf, f) || (buf[strcspn(buf, "\r\n")] = '\0', !args_number(buf, 1, 3, &slot))) {
			if (f) fclose(f);
			return out_error(TC_SESSION, "no usable %s (the pairing slot of the protected key, 1-3)", path);
		}
		fclose(f);
	}
	rc = keys_select("unit", &boot, desc, sizeof desc); /* the bootstrap key: it may run MAC-and-Destroy and nothing else */
	if (rc) return rc;
	st = chip_session(x->chip, &boot, err);
	memset(&boot, 0, sizeof boot);
	if (st != CHIP_OK) return out_chip_error(st, "bootstrap session failed", err);
	rc = pin_read(x->pin_file, x->pin_stdin, "PIN: ", pin, sizeof pin);
	if (rc) return rc;
	ms = calloc(1, sizeof *ms);
	if (!ms) return out_error(TC_INTERNAL, "out of memory");
	snprintf(path, sizeof path, "%s/pin.state", keys_dir());
	if (mnd_state_load(path, ms) != 0) {
		free(ms);
		return out_error(TC_SESSION, "cannot read the PIN state %s", path);
	}
	snprintf(path, sizeof path, "%s/pin.add", keys_dir());
	if (access(path, F_OK) == 0 && file_read(path, add, sizeof add, &addlen) != 0) {
		free(ms);
		return out_error(TC_SESSION, "cannot read %s", path);
	}
	snprintf(path, sizeof path, "%s/pin.state", keys_dir());
	mr = mnd_check(x->chip, pin, add, addlen, ms, path, k, &cst, err);
	memset(pin, 0, sizeof pin);
	if (mr == MND_WRONG_PIN) {
		rc = out_error(TC_VERIFY, "wrong PIN: %d attempt(s) left", ms->remaining);
	} else if (mr == MND_NO_ATTEMPTS) {
		rc = out_error(TC_REFUSED, "no PIN attempts remain: the protected key can no longer be opened");
	} else if (mr == MND_CHIP) {
		rc = out_chip_error(cst, "the PIN check failed on the chip", err);
	} else if (mr != MND_OK) {
		rc = out_error(TC_SESSION, "the PIN state is unusable or the PIN is not 4 to 8 characters");
	} else {
		rc = TC_OK;
	}
	free(ms);
	if (rc) return rc;
	snprintf(path, sizeof path, "%s/protected.enc", keys_dir());
	if (file_read(path, enc, sizeof enc, &enclen) != 0 || enclen != 60 || crypto_open(k, enc, enclen, priv) != 32) {
		memset(k, 0, sizeof k);
		return out_error(TC_SESSION, "the protected key %s does not open with the released key", path);
	}
	memset(k, 0, sizeof k);
	if (crypto_x25519_public(priv, prot.pub) != 0) {
		memset(priv, 0, sizeof priv);
		return out_error(TC_INTERNAL, "cannot derive the public key of the protected key");
	}
	memcpy(prot.priv, priv, 32);
	memset(priv, 0, sizeof priv);
	prot.slot = (int)slot;
	chip_session_end(x->chip, err);
	st = chip_session(x->chip, &prot, err);
	memset(&prot, 0, sizeof prot);
	if (st != CHIP_OK) return out_chip_error(st, "session with the protected key failed", err);
	snprintf(x->key_desc, sizeof x->key_desc, "PIN-protected key, slot %ld", slot);
	x->key.slot = (int)slot;
	x->have_session = 1;
	return TC_OK;
}

int ctx_session(ctx_t *x)
{
	char err[CHIP_ERRLEN] = "";
	chip_status_t st;
	int rc = ctx_chip(x);
	if (rc) return rc;
	if (x->have_session) return TC_OK;
	if (x->key_source && strcmp(x->key_source, "pin") == 0) return session_pin(x);
	rc = keys_select(x->key_source, &x->key, x->key_desc, sizeof x->key_desc);
	if (rc) return rc;
	st = chip_session(x->chip, &x->key, err);
	if (st != CHIP_OK) {
		rc = out_chip_error(st, "secure session failed", err);
		if (st == CHIP_E_SESSION && (!x->key_source || strcmp(x->key_source, "shared") == 0 || strcmp(x->key_source, "es01") == 0 || strcmp(x->key_source, "auto") == 0) && x->key.slot == 0)
			out_notice("hint: a chip with production provisioning refuses this key and uses the prod0 slot-0 key; try --key prod0, or set it once: echo prod0 > %s/slot0-key (or TROPICCTL_SH0=prod0)", keys_dir());
		return rc;
	}
	x->have_session = 1;
	return TC_OK;
}

void ctx_close(ctx_t *x)
{
	if (x->chip) chip_close(x->chip);
	x->chip = NULL;
	memset(&x->key, 0, sizeof x->key); /* the private half must not linger */
}
