/*
 * mnd.h -- the PIN-verification scheme of Tropic Square's application note ODN_TR01_app_002 (MAC-and-Destroy), built
 * on the chip's MAC-and-Destroy sequence. The attempt limit lives in the chip's slots; the host stores only
 * ciphertexts of a master secret, a tag and the number of remaining attempts, none of which needs protection.
 */
#ifndef TROPICCTL_MND_H
#define TROPICCTL_MND_H

#include <stddef.h>
#include <stdint.h>

#include "chip.h"

#define MND_PIN_MIN 4
#define MND_PIN_MAX 8
#define MND_ADD_MAX 128
#define MND_SLOTS_MAX 127

typedef enum { MND_OK = 0, MND_WRONG_PIN, MND_NO_ATTEMPTS, MND_CHIP, MND_STATE, MND_PARAM } mnd_result_t;

typedef struct {
	uint8_t n;         /* attempts allowed (number of chip slots used) */
	uint8_t remaining; /* attempts left */
	uint8_t base;      /* first chip slot used */
	uint8_t tag[32];
	uint8_t c[MND_SLOTS_MAX][60]; /* nonce(12) || sealed master secret(32) || tag(16) */
} mnd_state_t;

/* New PIN setup: initialises n chip slots from `base`, fills the state and returns the 32-byte key k. */
mnd_result_t mnd_setup(chip_t *c, const char *pin, const uint8_t *add, size_t addlen, int n, int base, mnd_state_t *st,
                       uint8_t k[32], chip_status_t *cst, char *err);
/*
 * PIN entry check. The decremented attempt counter is saved to state_path BEFORE the chip is touched (a crash or a
 * power cut after the chip destroyed a slot must not hand out a free attempt), and again after a success. On
 * MND_WRONG_PIN an attempt has been used up.
 */
mnd_result_t mnd_check(chip_t *c, const char *pin, const uint8_t *add, size_t addlen, mnd_state_t *st,
                       const char *state_path, uint8_t k[32], chip_status_t *cst, char *err);

int mnd_state_load(const char *path, mnd_state_t *st);   /* 0 ok */
int mnd_state_save(const char *path, const mnd_state_t *st); /* atomic: temp file, fsync, rename; 0 ok */

#endif
