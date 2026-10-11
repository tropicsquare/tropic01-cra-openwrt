/* crypto.h -- the host-side cryptography tropicctl needs (OpenSSL): keyed hash, authenticated encryption for the PIN
 * scheme, randomness, and small encoders. The chip's own cryptography is never done here. */
#ifndef TROPICCTL_CRYPTO_H
#define TROPICCTL_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

int crypto_random(uint8_t *buf, size_t n);                                    /* 0 ok */
void crypto_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void crypto_hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *data, size_t dlen, uint8_t out[32]);
/* AES-256-GCM: out = nonce(12) || ciphertext(ptlen) || tag(16); returns bytes written or -1 */
int crypto_seal(const uint8_t key[32], const uint8_t *pt, size_t ptlen, uint8_t *out);
/* the reverse; returns the plaintext length, or -1 if the tag does not verify (wrong key or damaged data) */
int crypto_open(const uint8_t key[32], const uint8_t *in, size_t inlen, uint8_t *pt);
#define CRYPTO_SEAL_OVERHEAD 28
int crypto_x25519_public(const uint8_t priv[32], uint8_t pub[32]);            /* 0 ok */
size_t crypto_base64(const uint8_t *in, size_t n, char *out, size_t cap);     /* returns length, 0 on overflow */
int crypto_ecdsa_raw_to_der(const uint8_t raw[64], uint8_t *der, size_t cap); /* returns length or -1 */
/* SubjectPublicKeyInfo PEM for a raw public key (P-256: 64 bytes X||Y, Ed25519: 32 bytes); returns length or 0 */
size_t crypto_spki_pem(int curve_p256, const uint8_t *pub, char *out, size_t cap);

#endif
