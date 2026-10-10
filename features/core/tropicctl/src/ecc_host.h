/* ecc_host.h -- ECC on the host with OpenSSL. Used ONLY by the fake chip, so that sign commands can be checked end
 * to end with real signatures. The real chip never exposes a private key. */
#ifndef TROPICCTL_ECC_HOST_H
#define TROPICCTL_ECC_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "chip.h"

int ecc_host_generate(chip_curve_t curve, uint8_t priv[32]);                                   /* 0 ok */
int ecc_host_public(chip_curve_t curve, const uint8_t priv[32], uint8_t pub[64], size_t *publen); /* 0 ok */
int ecc_host_ecdsa(const uint8_t priv[32], const uint8_t hash[32], uint8_t sig[64]);          /* raw r||s */
int ecc_host_eddsa(const uint8_t priv[32], const uint8_t *msg, size_t len, uint8_t sig[64]);

#endif
