/* keys.h -- which pairing key opens the session (SPEC.md section 5). */
#ifndef TROPICCTL_KEYS_H
#define TROPICCTL_KEYS_H

#include <stddef.h>

#include "chip.h"

/*
 * source: NULL or "auto", "shared", "unit", "file:PATH:SLOT" (PATH is the 32-byte private key, PATH.pub the public key),
 * "pin" (not available in this version). The per-unit files live in $TROPICCTL_KEY_DIR (default /etc/tropic01):
 * pairing.key, pairing.pub, pairing.slot. Files that exist but are unusable are an error, never a silent fallback.
 * Returns 0 or a TC_* exit code (the message has been printed).
 */
int keys_select(const char *source, pairing_key_t *out, char *desc, size_t desclen);

/* the directory holding the per-unit key files and the PIN-protected key files */
const char *keys_dir(void);

#endif
