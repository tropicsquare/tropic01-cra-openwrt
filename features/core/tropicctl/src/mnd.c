#include "mnd.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "crypto.h"

static const uint8_t ZERO32[32] = {0};

/* KDF(key, x) = HMAC-SHA256(key, x), as the application note allows */
static void kdf(const uint8_t key[32], const uint8_t *x, size_t xl, uint8_t out[32])
{
	crypto_hmac_sha256(key, 32, x, xl, out);
}
static void kdf1(const uint8_t key[32], uint8_t byte, uint8_t out[32]) { kdf(key, &byte, 1, out); }

static size_t pin_add(const char *pin, const uint8_t *add, size_t addlen, uint8_t *buf)
{
	size_t pl = strlen(pin);
	memcpy(buf, pin, pl);
	if (addlen) memcpy(buf + pl, add, addlen);
	return pl + addlen;
}

static int valid(const char *pin, size_t addlen)
{
	size_t pl = pin ? strlen(pin) : 0;
	return pl >= MND_PIN_MIN && pl <= MND_PIN_MAX && addlen <= MND_ADD_MAX;
}

mnd_result_t mnd_setup(chip_t *c, const char *pin, const uint8_t *add, size_t addlen, int n, int base, mnd_state_t *st,
                       uint8_t k[32], chip_status_t *cst, char *err)
{
	uint8_t s[32], u[32], v[32], w[32], ki[32], garbage[32], pa[MND_PIN_MAX + MND_ADD_MAX];
	size_t pal;
	if (!valid(pin, addlen) || n < 1 || n > MND_SLOTS_MAX || base < 0 || base + n > CHIP_MND_SLOTS) return MND_PARAM;
	memset(st, 0, sizeof *st);
	st->n = (uint8_t)n;
	st->remaining = (uint8_t)n; /* 1 */
	st->base = (uint8_t)base;
	if (crypto_random(s, 32) != 0) return MND_STATE; /* 2 */
	kdf1(s, 0x00, st->tag);                          /* 3 */
	kdf1(s, 0x01, u);                                /* 4 */
	pal = pin_add(pin, add, addlen, pa);
	kdf(ZERO32, pa, pal, v);                         /* 5 */
	for (int i = 0; i < n; i++) {                    /* 6 */
		*cst = chip_mnd(c, (unsigned)(base + i), u, garbage, err);
		if (*cst != CHIP_OK) return MND_CHIP;
		*cst = chip_mnd(c, (unsigned)(base + i), v, w, err);
		if (*cst != CHIP_OK) return MND_CHIP;
		kdf(w, pa, pal, ki);
		if (crypto_seal(ki, s, 32, st->c[i]) != 60) return MND_STATE;
		*cst = chip_mnd(c, (unsigned)(base + i), u, garbage, err);
		if (*cst != CHIP_OK) return MND_CHIP;
	}
	kdf1(s, 0x02, k); /* 7 */
	memset(s, 0, sizeof s); memset(ki, 0, sizeof ki); memset(w, 0, sizeof w); memset(pa, 0, sizeof pa);
	return MND_OK;
}

mnd_result_t mnd_check(chip_t *c, const char *pin, const uint8_t *add, size_t addlen, mnd_state_t *st,
                       const char *state_path, uint8_t k[32], chip_status_t *cst, char *err)
{
	uint8_t s[32], t[32], u[32], v[32], w[32], ki[32], garbage[32], pa[MND_PIN_MAX + MND_ADD_MAX];
	size_t pal;
	int i, pl;
	if (!valid(pin, addlen)) return MND_PARAM;
	if (st->n < 1 || st->remaining > st->n) return MND_STATE;
	if (st->remaining == 0) return MND_NO_ATTEMPTS; /* 1, 2 */
	st->remaining--;                                /* 3 */
	if (state_path && mnd_state_save(state_path, st) != 0) {
		st->remaining++;
		return MND_STATE; /* could not record the attempt, so the chip is not touched */
	}
	i = st->remaining;
	pal = pin_add(pin, add, addlen, pa);
	kdf(ZERO32, pa, pal, v);                        /* 4 */
	*cst = chip_mnd(c, (unsigned)(st->base + i), v, w, err); /* 5: destroys the slot */
	if (*cst != CHIP_OK) return MND_CHIP;
	kdf(w, pa, pal, ki);                            /* 6 */
	pl = crypto_open(ki, st->c[i], 60, s);          /* 7: fails if the PIN or the slot was wrong */
	memset(ki, 0, sizeof ki); memset(w, 0, sizeof w); memset(pa, 0, sizeof pa);
	if (pl != 32) return MND_WRONG_PIN;
	kdf1(s, 0x00, t);                               /* 8 */
	if (memcmp(t, st->tag, 32) != 0) {              /* 9 */
		memset(s, 0, sizeof s);
		return MND_WRONG_PIN;
	}
	kdf1(s, 0x01, u);                               /* 10 */
	for (int j = i; j < st->n; j++) {               /* 11: initialise the destroyed slots again */
		*cst = chip_mnd(c, (unsigned)(st->base + j), u, garbage, err);
		if (*cst != CHIP_OK) {
			memset(s, 0, sizeof s);
			return MND_CHIP;
		}
	}
	st->remaining = st->n;                          /* 12 */
	if (state_path && mnd_state_save(state_path, st) != 0) {
		memset(s, 0, sizeof s);
		return MND_STATE;
	}
	kdf1(s, 0x02, k);                               /* 13 */
	memset(s, 0, sizeof s);
	return MND_OK;
}

#define MAGIC "TCMND1\0\0"

int mnd_state_load(const char *path, mnd_state_t *st)
{
	uint8_t head[8 + 3 + 32];
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	memset(st, 0, sizeof *st);
	if (fread(head, 1, sizeof head, f) != sizeof head || memcmp(head, MAGIC, 8) != 0) {
		fclose(f);
		return -1;
	}
	st->n = head[8];
	st->remaining = head[9];
	st->base = head[10];
	memcpy(st->tag, head + 11, 32);
	if (st->n < 1 || st->n > MND_SLOTS_MAX || st->remaining > st->n) {
		fclose(f);
		return -1;
	}
	for (int i = 0; i < st->n; i++)
		if (fread(st->c[i], 1, 60, f) != 60) {
			fclose(f);
			return -1;
		}
	fclose(f);
	return 0;
}

int mnd_state_save(const char *path, const mnd_state_t *st)
{
	char tmp[400];
	uint8_t head[8 + 3 + 32];
	int fd;
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	memcpy(head, MAGIC, 8);
	head[8] = st->n;
	head[9] = st->remaining;
	head[10] = st->base;
	memcpy(head + 11, st->tag, 32);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return -1;
	if (write(fd, head, sizeof head) != (ssize_t)sizeof head) goto fail;
	for (int i = 0; i < st->n; i++)
		if (write(fd, st->c[i], 60) != 60) goto fail;
	if (fsync(fd) != 0) goto fail;
	if (close(fd) != 0) {
		unlink(tmp);
		return -1;
	}
	return rename(tmp, path) == 0 ? 0 : -1;
fail:
	close(fd);
	unlink(tmp);
	return -1;
}
