#include "ecc_host.h"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <string.h>

#include "crypto.h"

/* the uncompressed public point (04 || X || Y) of a P-256 private scalar; 0 ok, -1 if the scalar is not in 1..n-1 */
static int p256_point(const uint8_t priv[32], uint8_t pub[65])
{
	EC_GROUP *grp = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
	EC_POINT *pt = grp ? EC_POINT_new(grp) : NULL;
	BN_CTX *bc = BN_CTX_new();
	BIGNUM *bn = BN_bin2bn(priv, 32, NULL);
	int ok = grp && pt && bc && bn && !BN_is_zero(bn) && BN_cmp(bn, EC_GROUP_get0_order(grp)) < 0 &&
	         EC_POINT_mul(grp, pt, bn, NULL, NULL, bc) == 1 &&
	         EC_POINT_point2oct(grp, pt, POINT_CONVERSION_UNCOMPRESSED, pub, 65, bc) == 65;
	BN_free(bn);
	BN_CTX_free(bc);
	EC_POINT_free(pt);
	EC_GROUP_free(grp);
	return ok ? 0 : -1;
}

/* a P-256 key pair from the raw private scalar; OpenSSL 3.0 does not derive the public key on import, so compute it */
static EVP_PKEY *p256_from_priv(const uint8_t priv[32])
{
	EVP_PKEY *pk = NULL;
	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
	OSSL_PARAM_BLD *b = OSSL_PARAM_BLD_new();
	OSSL_PARAM *p = NULL;
	BIGNUM *bn = BN_bin2bn(priv, 32, NULL);
	uint8_t pub[65];
	if (!ctx || !b || !bn || p256_point(priv, pub) != 0) goto done;
	if (!OSSL_PARAM_BLD_push_utf8_string(b, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0) ||
	    !OSSL_PARAM_BLD_push_BN(b, OSSL_PKEY_PARAM_PRIV_KEY, bn) ||
	    !OSSL_PARAM_BLD_push_octet_string(b, OSSL_PKEY_PARAM_PUB_KEY, pub, sizeof pub))
		goto done;
	p = OSSL_PARAM_BLD_to_param(b);
	if (!p || EVP_PKEY_fromdata_init(ctx) != 1 || EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_KEYPAIR, p) != 1) pk = NULL;
done:
	OSSL_PARAM_free(p);
	OSSL_PARAM_BLD_free(b);
	BN_free(bn);
	EVP_PKEY_CTX_free(ctx);
	return pk;
}

int ecc_host_generate(chip_curve_t curve, uint8_t priv[32])
{
	for (int tries = 0; tries < 64; tries++) {
		EVP_PKEY *pk;
		if (crypto_random(priv, 32) != 0) return -1;
		if (curve == CHIP_CURVE_ED25519) return 0;
		pk = p256_from_priv(priv); /* rejects values outside 1..n-1 */
		if (pk) {
			EVP_PKEY_free(pk);
			return 0;
		}
	}
	return -1;
}

int ecc_host_public(chip_curve_t curve, const uint8_t priv[32], uint8_t pub[64], size_t *publen)
{
	if (curve == CHIP_CURVE_ED25519) {
		size_t n = 32;
		EVP_PKEY *k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, priv, 32);
		int ok;
		if (!k) return -1;
		ok = EVP_PKEY_get_raw_public_key(k, pub, &n) == 1 && n == 32;
		EVP_PKEY_free(k);
		*publen = 32;
		return ok ? 0 : -1;
	} else {
		uint8_t buf[65];
		if (p256_point(priv, buf) != 0) return -1;
		memcpy(pub, buf + 1, 64);
		*publen = 64;
		return 0;
	}
}

int ecc_host_ecdsa(const uint8_t priv[32], const uint8_t hash[32], uint8_t sig[64])
{
	EVP_PKEY *k = p256_from_priv(priv);
	EVP_PKEY_CTX *ctx;
	uint8_t der[80];
	const uint8_t *p = der;
	size_t dl = sizeof der;
	ECDSA_SIG *s = NULL;
	int ok = 0;
	if (!k) return -1;
	ctx = EVP_PKEY_CTX_new(k, NULL);
	if (ctx && EVP_PKEY_sign_init(ctx) == 1 && EVP_PKEY_sign(ctx, der, &dl, hash, 32) == 1 &&
	    (s = d2i_ECDSA_SIG(NULL, &p, (long)dl)) != NULL) {
		const BIGNUM *r, *sv;
		ECDSA_SIG_get0(s, &r, &sv);
		ok = BN_bn2binpad(r, sig, 32) == 32 && BN_bn2binpad(sv, sig + 32, 32) == 32;
	}
	ECDSA_SIG_free(s);
	EVP_PKEY_CTX_free(ctx);
	EVP_PKEY_free(k);
	return ok ? 0 : -1;
}

int ecc_host_eddsa(const uint8_t priv[32], const uint8_t *msg, size_t len, uint8_t sig[64])
{
	EVP_PKEY *k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, priv, 32);
	EVP_MD_CTX *m = EVP_MD_CTX_new();
	size_t sl = 64;
	int ok = k && m && EVP_DigestSignInit(m, NULL, NULL, NULL, k) == 1 && EVP_DigestSign(m, sig, &sl, msg, len) == 1 && sl == 64;
	EVP_MD_CTX_free(m);
	EVP_PKEY_free(k);
	return ok ? 0 : -1;
}
