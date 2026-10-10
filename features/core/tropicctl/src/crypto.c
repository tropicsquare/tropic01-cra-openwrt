#include "crypto.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <string.h>

int crypto_random(uint8_t *buf, size_t n) { return RAND_bytes(buf, (int)n) == 1 ? 0 : -1; }

void crypto_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
	unsigned int n = 32;
	EVP_Digest(data, len, out, &n, EVP_sha256(), NULL);
}

void crypto_hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *data, size_t dlen, uint8_t out[32])
{
	unsigned int n = 32;
	HMAC(EVP_sha256(), key, (int)klen, data, dlen, out, &n);
}

int crypto_seal(const uint8_t key[32], const uint8_t *pt, size_t ptlen, uint8_t *out)
{
	EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
	int len = 0, total = 0, ok = 0;
	if (!c || crypto_random(out, 12) != 0) goto done;
	if (EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), NULL, key, out) != 1) goto done;
	if (EVP_EncryptUpdate(c, out + 12, &len, pt, (int)ptlen) != 1) goto done;
	total = len;
	if (EVP_EncryptFinal_ex(c, out + 12 + total, &len) != 1) goto done;
	total += len;
	if (EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, out + 12 + total) != 1) goto done;
	ok = 1;
done:
	EVP_CIPHER_CTX_free(c);
	return ok ? 12 + total + 16 : -1;
}

int crypto_open(const uint8_t key[32], const uint8_t *in, size_t inlen, uint8_t *pt)
{
	EVP_CIPHER_CTX *c;
	int len = 0, total = 0, ok = 0;
	uint8_t tag[16];
	if (inlen < CRYPTO_SEAL_OVERHEAD) return -1;
	c = EVP_CIPHER_CTX_new();
	if (!c) return -1;
	memcpy(tag, in + inlen - 16, 16);
	if (EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), NULL, key, in) != 1) goto done;
	if (EVP_DecryptUpdate(c, pt, &len, in + 12, (int)(inlen - 28)) != 1) goto done;
	total = len;
	if (EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag) != 1) goto done;
	if (EVP_DecryptFinal_ex(c, pt + total, &len) != 1) goto done; /* fails when the tag does not verify */
	total += len;
	ok = 1;
done:
	EVP_CIPHER_CTX_free(c);
	return ok ? total : -1;
}

int crypto_x25519_public(const uint8_t priv[32], uint8_t pub[32])
{
	size_t n = 32;
	EVP_PKEY *k = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv, 32);
	int ok;
	if (!k) return -1;
	ok = EVP_PKEY_get_raw_public_key(k, pub, &n) == 1 && n == 32;
	EVP_PKEY_free(k);
	return ok ? 0 : -1;
}

size_t crypto_base64(const uint8_t *in, size_t n, char *out, size_t cap)
{
	size_t need = 4 * ((n + 2) / 3);
	if (need + 1 > cap) return 0;
	EVP_EncodeBlock((unsigned char *)out, in, (int)n);
	return need;
}

int crypto_ecdsa_raw_to_der(const uint8_t raw[64], uint8_t *der, size_t cap)
{
	/* SEQUENCE { INTEGER r, INTEGER s }, each integer minimal and positive */
	uint8_t body[80];
	size_t bl = 0;
	for (int part = 0; part < 2; part++) {
		const uint8_t *p = raw + 32 * part;
		size_t skip = 0, n;
		while (skip < 31 && p[skip] == 0) skip++;
		n = 32 - skip;
		body[bl++] = 0x02;
		if (p[skip] & 0x80) {
			body[bl++] = (uint8_t)(n + 1);
			body[bl++] = 0x00;
		} else {
			body[bl++] = (uint8_t)n;
		}
		memcpy(body + bl, p + skip, n);
		bl += n;
	}
	if (bl + 2 > cap) return -1;
	der[0] = 0x30;
	der[1] = (uint8_t)bl;
	memcpy(der + 2, body, bl);
	return (int)(bl + 2);
}

size_t crypto_spki_pem(int curve_p256, const uint8_t *pub, char *out, size_t cap)
{
	static const uint8_t P256[] = {0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08,
	                               0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04};
	static const uint8_t ED[] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};
	uint8_t der[128];
	char b64[200];
	size_t dl, bl, o = 0, i;
	if (curve_p256) {
		memcpy(der, P256, sizeof P256);
		memcpy(der + sizeof P256, pub, 64);
		dl = sizeof P256 + 64;
	} else {
		memcpy(der, ED, sizeof ED);
		memcpy(der + sizeof ED, pub, 32);
		dl = sizeof ED + 32;
	}
	bl = crypto_base64(der, dl, b64, sizeof b64);
	if (!bl) return 0;
	o += (size_t)snprintf(out + o, cap - o, "-----BEGIN PUBLIC KEY-----\n");
	for (i = 0; i < bl; i += 64) o += (size_t)snprintf(out + o, cap - o, "%.*s\n", (int)(bl - i < 64 ? bl - i : 64), b64 + i);
	o += (size_t)snprintf(out + o, cap - o, "-----END PUBLIC KEY-----\n");
	return o < cap ? o : 0;
}
