#include <stdio.h>
#include <string.h>

#include "cmd_table.h"
#include "crypto.h"
#include "errors.h"
#include "files.h"
#include "output.h"

#define SIGN_MSG_MAX 4095

static const char *curve_name(chip_curve_t c) { return c == CHIP_CURVE_ED25519 ? "ed25519" : "p256"; }
static const char *origin_name(chip_origin_t o) { return o == CHIP_ORIGIN_STORED ? "stored" : "generated"; }

static int slot_arg(const cmd_t *c, args_t *a, unsigned *slot)
{
	long v;
	if (a->argc != 1 || !args_number(a->argv[0], 0, CHIP_ECC_SLOTS - 1, &v)) return cmd_usage(c, "slot must be 0 to 31");
	*slot = (unsigned)v;
	return TC_OK;
}

static int curve_arg(const cmd_t *c, args_t *a, chip_curve_t *cv)
{
	const char *v = NULL;
	int r = args_value(a, "--curve", &v);
	if (r <= 0) return cmd_usage(c, "--curve p256|ed25519 is required");
	if (strcmp(v, "p256") == 0) *cv = CHIP_CURVE_P256;
	else if (strcmp(v, "ed25519") == 0) *cv = CHIP_CURVE_ED25519;
	else return cmd_usage(c, "--curve must be p256 or ed25519");
	return TC_OK;
}

int cmd_ecc_list(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("ecc", "list");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	uint8_t pub[64];
	size_t pl;
	chip_curve_t cv;
	chip_origin_t og;
	int rc, first = 1;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if ((rc = ctx_session(x))) return rc;
	if (g_json) printf("{\"ecc_slots\":[");
	for (unsigned s = 0; s < CHIP_ECC_SLOTS; s++) {
		chip_status_t st = chip_ecc_read(x->chip, s, pub, &pl, &cv, &og, err);
		if (st != CHIP_OK && st != CHIP_E_EMPTY) return out_chip_error(st, "cannot read an ECC slot", err);
		if (g_json) {
			if (st == CHIP_OK) printf("%s{\"slot\":%u,\"state\":\"populated\",\"curve\":\"%s\",\"origin\":\"%s\"}", first ? "" : ",", s, curve_name(cv), origin_name(og));
			else printf("%s{\"slot\":%u,\"state\":\"empty\"}", first ? "" : ",", s);
			first = 0;
		} else if (st == CHIP_OK) printf("ecc slot %u: %s %s\n", s, curve_name(cv), origin_name(og));
		else printf("ecc slot %u: empty\n", s);
	}
	if (g_json) printf("]}\n");
	return TC_OK;
}

/* erase a key that is in the way (--replace), only with the confirmation flag: the key is lost for good */
static int clear_for_replace(ctx_t *x, int yes, unsigned slot, int replace, chip_status_t occupied)
{
	char err[CHIP_ERRLEN] = "";
	chip_status_t st;
	if (occupied != CHIP_OK) return TC_OK; /* the slot is empty: nothing in the way */
	if (!replace)
		return out_error(TC_OCCUPIED, "ECC slot %u already holds a key; --replace --yes-erase-ecc-key would erase it (the key is lost for good)", slot);
	if (!yes) {
		if (x->dry_run) return TC_OK; /* reported by the caller */
		return out_error(TC_REFUSED, "replacing an ECC key destroys it for good: give --yes-erase-ecc-key to confirm (or use --dry-run)");
	}
	if (x->dry_run) return TC_OK;
	st = chip_ecc_erase(x->chip, slot, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot erase the old key", err);
	return TC_OK;
}

static int verify_key(ctx_t *x, unsigned slot, chip_curve_t cv, chip_origin_t og, uint8_t pub[64], size_t *pl)
{
	char err[CHIP_ERRLEN] = "";
	chip_curve_t c2;
	chip_origin_t o2;
	chip_status_t st = chip_ecc_read(x->chip, slot, pub, pl, &c2, &o2, err);
	if (st != CHIP_OK) return out_error(TC_VERIFY, "the key was written but cannot be read back: %s", err);
	if (c2 != cv || o2 != og) return out_error(TC_VERIFY, "ECC slot %u does not read back as a %s key that was %s", slot, curve_name(cv), origin_name(og));
	return TC_OK;
}

int cmd_ecc_generate(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("ecc", "generate");
	char err[CHIP_ERRLEN] = "", hex[129];
	const char *bad;
	uint8_t pub[64];
	size_t pl;
	unsigned slot;
	chip_curve_t cv, c0;
	chip_origin_t og;
	chip_status_t cur, st;
	int rc, replace = args_flag(a, "--replace"), yes = args_flag(a, "--yes-erase-ecc-key");
	if ((rc = curve_arg(c, a, &cv))) return rc;
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = slot_arg(c, a, &slot))) return rc;
	if ((rc = ctx_session(x))) return rc;
	cur = chip_ecc_read(x->chip, slot, pub, &pl, &c0, &og, err);
	if (cur != CHIP_OK && cur != CHIP_E_EMPTY) return out_chip_error(cur, "cannot check the slot first", err);
	if ((rc = clear_for_replace(x, yes, slot, replace, cur))) return rc;
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): would generate a %s key in ECC slot %u%s", curve_name(cv), slot,
		           cur == CHIP_OK ? " after erasing the old key (needs --yes-erase-ecc-key)" : "");
		return TC_OK;
	}
	st = chip_ecc_generate(x->chip, slot, cv, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot generate the key", err);
	if ((rc = verify_key(x, slot, cv, CHIP_ORIGIN_GENERATED, pub, &pl))) return rc;
	hex_to(hex, pub, pl);
	if (g_json) printf("{\"slot\":%u,\"curve\":\"%s\",\"origin\":\"generated\",\"public_key\":\"%s\"}\n", slot, curve_name(cv), hex);
	else printf("ecc slot %u: generated a %s key; public key %s\n", slot, curve_name(cv), hex);
	return TC_OK;
}

int cmd_ecc_store(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("ecc", "store");
	char err[CHIP_ERRLEN] = "";
	const char *in = NULL, *bad;
	uint8_t priv[33], pub[64];
	size_t n = 0, pl;
	unsigned slot;
	chip_curve_t cv, c0;
	chip_origin_t og;
	chip_status_t cur, st;
	int rc, replace = args_flag(a, "--replace"), yes = args_flag(a, "--yes-erase-ecc-key");
	if ((rc = curve_arg(c, a, &cv))) return rc;
	if (args_value(a, "--in", &in) <= 0) return cmd_usage(c, "--in FILE is required");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = slot_arg(c, a, &slot))) return rc;
	if (file_read(in, priv, sizeof priv, &n) != 0 || n != 32) return out_error(TC_USAGE, "%s must hold exactly 32 bytes (the private key)", in);
	if ((rc = ctx_session(x))) {
		memset(priv, 0, sizeof priv);
		return rc;
	}
	cur = chip_ecc_read(x->chip, slot, pub, &pl, &c0, &og, err);
	if (cur != CHIP_OK && cur != CHIP_E_EMPTY) {
		memset(priv, 0, sizeof priv);
		return out_chip_error(cur, "cannot check the slot first", err);
	}
	if ((rc = clear_for_replace(x, yes, slot, replace, cur))) {
		memset(priv, 0, sizeof priv);
		return rc;
	}
	if (x->dry_run) {
		memset(priv, 0, sizeof priv);
		out_notice("DRY RUN (nothing written): would store a %s private key in ECC slot %u", curve_name(cv), slot);
		return TC_OK;
	}
	st = chip_ecc_store(x->chip, slot, cv, priv, err);
	memset(priv, 0, sizeof priv);
	if (st != CHIP_OK) return out_chip_error(st, "cannot store the key", err);
	if ((rc = verify_key(x, slot, cv, CHIP_ORIGIN_STORED, pub, &pl))) return rc;
	if (g_json) printf("{\"slot\":%u,\"curve\":\"%s\",\"origin\":\"stored\"}\n", slot, curve_name(cv));
	else printf("ecc slot %u: stored a %s key (origin: stored, not generated in the chip)\n", slot, curve_name(cv));
	return TC_OK;
}

int cmd_ecc_pub(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("ecc", "pub");
	char err[CHIP_ERRLEN] = "", hex[129], pem[400];
	const char *out = NULL, *fmt = "raw", *bad;
	uint8_t pub[64];
	size_t pl;
	unsigned slot;
	chip_curve_t cv;
	chip_origin_t og;
	chip_status_t st;
	int rc;
	if (args_value(a, "--out", &out) < 0 || args_value(a, "--format", &fmt) < 0) return cmd_usage(c, "an option needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (strcmp(fmt, "raw") != 0 && strcmp(fmt, "pem") != 0) return cmd_usage(c, "--format must be raw or pem");
	if ((rc = slot_arg(c, a, &slot))) return rc;
	if ((rc = ctx_session(x))) return rc;
	st = chip_ecc_read(x->chip, slot, pub, &pl, &cv, &og, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the public key", err);
	hex_to(hex, pub, pl);
	if (out) {
		int w;
		if (strcmp(fmt, "pem") == 0) {
			size_t n = crypto_spki_pem(cv == CHIP_CURVE_P256, pub, pem, sizeof pem);
			w = n ? file_write(out, (uint8_t *)pem, n) : -1;
		} else {
			w = file_write(out, pub, pl);
		}
		if (w != 0) return out_error(TC_INTERNAL, "cannot write %s", out);
		if (g_json && strcmp(out, "-") != 0) {
			printf("{\"slot\":%u,\"curve\":\"%s\",\"origin\":\"%s\",\"bytes\":%zu}\n", slot, curve_name(cv), origin_name(og), pl);
		}
		return TC_OK;
	}
	if (strcmp(fmt, "pem") == 0) {
		size_t n = crypto_spki_pem(cv == CHIP_CURVE_P256, pub, pem, sizeof pem);
		if (!n) return out_error(TC_INTERNAL, "cannot encode the key");
		if (g_json) {
			printf("{\"slot\":%u,\"curve\":\"%s\",\"pem\":", slot, curve_name(cv));
			json_str(stdout, pem);
			printf("}\n");
		} else fputs(pem, stdout);
		return TC_OK;
	}
	if (g_json) printf("{\"slot\":%u,\"curve\":\"%s\",\"origin\":\"%s\",\"public_key\":\"%s\"}\n", slot, curve_name(cv), origin_name(og), hex);
	else printf("%s\n", hex);
	return TC_OK;
}

int cmd_ecc_erase(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("ecc", "erase");
	char err[CHIP_ERRLEN] = "";
	const char *bad;
	uint8_t pub[64];
	size_t pl;
	unsigned slot;
	chip_curve_t cv;
	chip_origin_t og;
	chip_status_t st;
	int rc, ok;
	ok = args_flag(a, "--yes-erase-ecc-key");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if ((rc = slot_arg(c, a, &slot))) return rc;
	if (!ok && !x->dry_run) return out_error(TC_REFUSED, "erasing an ECC key destroys it for good: give --yes-erase-ecc-key to confirm (or use --dry-run)");
	if ((rc = ctx_session(x))) return rc;
	st = chip_ecc_read(x->chip, slot, pub, &pl, &cv, &og, err);
	if (st == CHIP_E_EMPTY) {
		if (g_json) printf("{\"slot\":%u,\"erased\":false,\"note\":\"already empty\"}\n", slot);
		else printf("ecc slot %u is already empty\n", slot);
		return TC_OK;
	}
	if (st != CHIP_OK) return out_chip_error(st, "cannot check the slot first", err);
	if (x->dry_run) {
		out_notice("DRY RUN (nothing written): would erase the %s key in ECC slot %u%s", curve_name(cv), slot, ok ? "" : " (needs --yes-erase-ecc-key)");
		return TC_OK;
	}
	st = chip_ecc_erase(x->chip, slot, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot erase the key", err);
	st = chip_ecc_read(x->chip, slot, pub, &pl, &cv, &og, err);
	if (st != CHIP_E_EMPTY) return out_error(TC_VERIFY, "ECC slot %u still holds a key after the erase", slot);
	if (g_json) printf("{\"slot\":%u,\"erased\":true}\n", slot);
	else printf("ecc slot %u: erased\n", slot);
	return TC_OK;
}

int cmd_ecc_sign(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("ecc", "sign");
	char err[CHIP_ERRLEN] = "", hex[129];
	const char *in = NULL, *out = NULL, *alg = NULL, *bad;
	uint8_t msg[SIGN_MSG_MAX + 1], hash[32], pub[64], sig[64], der[80];
	size_t n = 0, pl;
	unsigned slot;
	chip_curve_t cv;
	chip_origin_t og;
	chip_status_t st;
	int rc, digest = args_flag(a, "--digest"), want_der = args_flag(a, "--der"), r;
	r = args_value(a, "--in", &in);
	if (r <= 0) return cmd_usage(c, "--in FILE is required");
	if (args_value(a, "--out", &out) < 0 || args_value(a, "--alg", &alg) < 0) return cmd_usage(c, "an option needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (alg && strcmp(alg, "ecdsa") != 0 && strcmp(alg, "eddsa") != 0) return cmd_usage(c, "--alg must be ecdsa or eddsa");
	if ((rc = slot_arg(c, a, &slot))) return rc;
	rc = file_read(in, msg, sizeof msg, &n);
	if (rc == -2) return out_error(TC_USAGE, "%s is longer than %d bytes", in, SIGN_MSG_MAX);
	if (rc != 0) return out_error(TC_USAGE, "cannot read %s", in);
	if ((rc = ctx_session(x))) return rc;
	st = chip_ecc_read(x->chip, slot, pub, &pl, &cv, &og, err);
	if (st != CHIP_OK) return out_chip_error(st, "cannot read the key slot", err);
	if (!alg) alg = cv == CHIP_CURVE_P256 ? "ecdsa" : "eddsa";
	if ((strcmp(alg, "ecdsa") == 0) != (cv == CHIP_CURVE_P256))
		return out_error(TC_USAGE, "ECC slot %u holds a %s key, which cannot sign with %s (use --alg %s)", slot, curve_name(cv), alg, cv == CHIP_CURVE_P256 ? "ecdsa" : "eddsa");
	if (strcmp(alg, "ecdsa") == 0) {
		if (digest) {
			if (n != 32) return out_error(TC_USAGE, "--digest needs exactly 32 bytes of input (a SHA-256 digest), got %zu", n);
			memcpy(hash, msg, 32);
		} else {
			crypto_sha256(msg, n, hash);
		}
		st = chip_ecdsa_sign(x->chip, slot, hash, sig, err);
	} else {
		if (digest || want_der) return cmd_usage(c, "--digest and --der apply to ECDSA only");
		if (n > SIGN_MSG_MAX) return out_error(TC_USAGE, "an EdDSA message is at most %d bytes, got %zu", SIGN_MSG_MAX, n);
		st = chip_eddsa_sign(x->chip, slot, msg, n, sig, err);
	}
	if (st != CHIP_OK) return out_chip_error(st, "signing failed", err);
	if (want_der) {
		int dl = crypto_ecdsa_raw_to_der(sig, der, sizeof der);
		if (dl < 0) return out_error(TC_INTERNAL, "cannot encode the signature");
		if (out) return file_write(out, der, (size_t)dl) == 0 ? TC_OK : out_error(TC_INTERNAL, "cannot write %s", out);
		hex_to(hex, der, (size_t)dl);
	} else {
		if (out) {
			if (file_write(out, sig, 64) != 0) return out_error(TC_INTERNAL, "cannot write %s", out);
			if (g_json && strcmp(out, "-") != 0) printf("{\"slot\":%u,\"alg\":\"%s\",\"bytes\":64}\n", slot, alg);
			return TC_OK;
		}
		hex_to(hex, sig, 64);
	}
	if (g_json) printf("{\"slot\":%u,\"alg\":\"%s\",\"signature\":\"%s\"}\n", slot, alg, hex);
	else printf("%s\n", hex);
	return TC_OK;
}
