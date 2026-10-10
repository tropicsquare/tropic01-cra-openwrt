/*
 * chip_fake.c -- a hand-written fake TROPIC01 for tests on a host. State lives in the directory named by
 * $TROPICCTL_FAKE_DIR (created with defaults when $TROPICCTL_FAKE_INIT=1):
 *
 *   pairing/N         32 bytes: the public key held in pairing slot N (missing = empty); pairing/N.invalid = invalidated
 *   rmem/N            content of R-MEM slot N (missing = empty)
 *   rconfig, iconfig  one "NAME 0xVALUE" line per configuration object
 *   fw                "RISCV SPECT" as 8 hex digits each
 *   down              if present, the chip is unreachable
 *   ecc/N.key .curve .origin   an ECC key (the fake keeps the private key; a real chip never exposes it)
 *   mnd/N             a MAC-and-Destroy slot (32 bytes); mnd_keys the two keys of the chip's keyed functions
 *   counters/N        a monotonic counter, decimal
 *   certs/N.der       a certificate of the chip's certificate store
 *
 * Failure modes for tests: TROPICCTL_FAKE_CORRUPT_WRITE=1 stores wrong content on an R-MEM write;
 * TROPICCTL_FAKE_SILENT_ERASE=1 makes an R-MEM erase do nothing while reporting success;
 * TROPICCTL_FAKE_PAIRING_CORRUPT=1 stores the wrong key on a pairing write;
 * TROPICCTL_FAKE_PAIRING_SILENT=1 reports a pairing write as done without storing it (old-firmware behaviour);
 * TROPICCTL_FAKE_CONFIG_FAIL_AFTER_ERASE=1 fails the R-config write after the erase;
 * TROPICCTL_FAKE_ICONFIG_SILENT=1 reports an I-config bit write as done without changing it;
 * TROPICCTL_FAKE_CONFIG_PARTIAL=1 writes the R-config with the last object left at its default;
 * TROPICCTL_FAKE_MND_FAIL=1 makes every MAC-and-Destroy sequence fail with a chip error.
 * [measured 2026-10-11] an ECC slot that was never written answers like an empty one, a counter that was never initialised
 * answers LT_L3_COUNTER_INVALID. [assumed] erasing the R-config returns the objects to the defaults the fake starts with.
 *
 * Every rule below is labelled:  [measured] checked on the bench chip (docs/cra/hardware-test-report.md), or
 * [assumed] taken from libtropic's headers or the datasheet and still to be confirmed on a chip. Tests that depend on
 * an assumed rule say so. The fake never claims to model the cryptography.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "chip.h"
#include "crypto.h"
#include "ecc_host.h"
#include "uap.h"

struct chip {
	char dir[400];
	int session_slot; /* -1 = no session */
};

/* [measured] the shared engineering-sample key is published with libtropic (lt_sh0priv_eng_sample, ES01 values) */
static const uint8_t SH0_PRIV[32] = {0xd0, 0x99, 0x92, 0xb1, 0xf1, 0x7a, 0xbc, 0x4d, 0xb9, 0x37, 0x17,
                                     0x68, 0xa2, 0x7d, 0xa0, 0x5b, 0x18, 0xfa, 0xb8, 0x56, 0x13, 0xa7,
                                     0x84, 0x2c, 0xa6, 0x4c, 0x79, 0x10, 0xf2, 0x2e, 0x71, 0x6b};
static const uint8_t SH0_PUB[32] = {0xe7, 0xf7, 0x35, 0xba, 0x19, 0xa3, 0x3f, 0xd6, 0x73, 0x23, 0xab,
                                    0x37, 0x26, 0x2d, 0xe5, 0x36, 0x08, 0xca, 0x57, 0x85, 0x76, 0x53,
                                    0x43, 0x52, 0xe1, 0x8f, 0x64, 0xe6, 0x13, 0xd3, 0x8d, 0x54};

static const char *const CFG_NAMES[CHIP_CONFIG_OBJ_CNT] = {
    "TR01_CFG_START_UP",
    "TR01_CFG_SENSORS",
    "TR01_CFG_DEBUG",
    "TR01_CFG_GPO_ADDR",
    "TR01_CFG_SLEEP_MODE",
    "TR01_CFG_UAP_PAIRING_KEY_WRITE",
    "TR01_CFG_UAP_PAIRING_KEY_READ",
    "TR01_CFG_UAP_PAIRING_KEY_INVALIDATE",
    "TR01_CFG_UAP_R_CONFIG_WRITE_ERASE",
    "TR01_CFG_UAP_R_CONFIG_READ",
    "TR01_CFG_UAP_I_CONFIG_WRITE",
    "TR01_CFG_UAP_I_CONFIG_READ",
    "TR01_CFG_UAP_PING",
    "TR01_CFG_UAP_R_MEM_DATA_WRITE",
    "TR01_CFG_UAP_R_MEM_DATA_READ",
    "TR01_CFG_UAP_R_MEM_DATA_ERASE",
    "TR01_CFG_UAP_RANDOM_VALUE_GET",
    "TR01_CFG_UAP_ECC_KEY_GENERATE",
    "TR01_CFG_UAP_ECC_KEY_STORE",
    "TR01_CFG_UAP_ECC_KEY_READ",
    "TR01_CFG_UAP_ECC_KEY_ERASE",
    "TR01_CFG_UAP_ECDSA_SIGN",
    "TR01_CFG_UAP_EDDSA_SIGN",
    "TR01_CFG_UAP_MCOUNTER_INIT",
    "TR01_CFG_UAP_MCOUNTER_GET",
    "TR01_CFG_UAP_MCOUNTER_UPDATE",
    "TR01_CFG_UAP_MAC_AND_DESTROY",
};

static void setmsg(char *err, const char *m) { snprintf(err, CHIP_ERRLEN, "%s", m); }

static void path_of(const chip_t *c, char *out, size_t n, const char *fmt, unsigned a)
{
	char rel[64];
	snprintf(rel, sizeof rel, fmt, a);
	snprintf(out, n, "%s/%s", c->dir, rel);
}

static int read_file(const char *p, uint8_t *buf, size_t cap, size_t *len)
{
	FILE *f = fopen(p, "rb");
	size_t n;
	if (!f) return -1;
	n = fread(buf, 1, cap, f);
	fclose(f);
	*len = n;
	return 0;
}

static int write_file(const char *p, const uint8_t *buf, size_t len)
{
	FILE *f = fopen(p, "wb");
	if (!f) return -1;
	if (fwrite(buf, 1, len, f) != len) {
		fclose(f);
		return -1;
	}
	return fclose(f);
}

static void make_defaults(const char *dir)
{
	char p[420];
	FILE *f;
	snprintf(p, sizeof p, "%s/pairing", dir); mkdir(dir, 0700); mkdir(p, 0700);
	snprintf(p, sizeof p, "%s/rmem", dir); mkdir(p, 0700);
	snprintf(p, sizeof p, "%s/pairing/0", dir);
	write_file(p, SH0_PUB, 32);
	/* [measured] bench chip, 2026-10-10: R-config all ones except START_UP 0xffffffef; I-config all ones; every access
	 * object 0xffffffff (all pairing keys may run every command) */
	snprintf(p, sizeof p, "%s/rconfig", dir);
	f = fopen(p, "w");
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) fprintf(f, "%s 0x%08x\n", CFG_NAMES[i], i == 0 ? 0xffffffefu : 0xffffffffu);
	fclose(f);
	snprintf(p, sizeof p, "%s/iconfig", dir);
	f = fopen(p, "w");
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) fprintf(f, "%s 0x%08x\n", CFG_NAMES[i], 0xffffffffu);
	fclose(f);
	snprintf(p, sizeof p, "%s/fw", dir);
	f = fopen(p, "w");
	fputs("00020100 00010300\n", f); /* [measured] the bench chip: bytes 00 02 01 00 and 00 01 03 00 = RISC-V FW 0.1.2, SPECT FW 0.3.1 */
	fclose(f);
}

chip_status_t chip_open(const chip_cfg_t *cfg, chip_t **out, char *err)
{
	const char *dir = getenv("TROPICCTL_FAKE_DIR");
	char p[420];
	struct stat st;
	chip_t *c;
	(void)cfg;
	if (!dir || !*dir) {
		setmsg(err, "TROPICCTL_FAKE_DIR is not set (this is the fake-chip build)");
		return CHIP_E_UNREACHABLE;
	}
	if (stat(dir, &st) != 0) {
		if (getenv("TROPICCTL_FAKE_INIT")) make_defaults(dir);
		else {
			setmsg(err, "fake chip state directory does not exist");
			return CHIP_E_UNREACHABLE;
		}
	}
	snprintf(p, sizeof p, "%s/down", dir);
	if (access(p, F_OK) == 0) {
		setmsg(err, "fake chip is down (no answer on the SPI bus)");
		return CHIP_E_UNREACHABLE;
	}
	c = calloc(1, sizeof *c);
	snprintf(c->dir, sizeof c->dir, "%s", dir);
	c->session_slot = -1;
	*out = c;
	return CHIP_OK;
}

void chip_close(chip_t *c) { free(c); }

chip_status_t chip_session_end(chip_t *c, char *err)
{
	(void)err;
	c->session_slot = -1;
	return CHIP_OK;
}

void chip_shared_key(pairing_key_t *out)
{
	memcpy(out->priv, SH0_PRIV, 32);
	memcpy(out->pub, SH0_PUB, 32);
	out->slot = 0;
}

chip_status_t chip_session(chip_t *c, const pairing_key_t *key, char *err)
{
	uint8_t have[32];
	size_t n = 0;
	char p[420];
	if (key->slot < 0 || key->slot >= CHIP_PAIRING_SLOTS) {
		setmsg(err, "LT_PARAM_ERR");
		return CHIP_E_PARAM;
	}
	snprintf(p, sizeof p, "%s/pairing/%d.invalid", c->dir, key->slot);
	if (access(p, F_OK) == 0) {
		setmsg(err, "LT_L2_HSK_ERR (this pairing key was invalidated)");
		return CHIP_E_SESSION;
	}
	path_of(c, p, sizeof p, "pairing/%u", (unsigned)key->slot);
	/* [assumed] a session opens only if the chip's slot holds the public key matching the host's private key; the fake
	 * compares the public halves */
	if (read_file(p, have, 32, &n) != 0 || n != 32 || memcmp(have, key->pub, 32) != 0) {
		setmsg(err, "LT_L2_HSK_ERR (the chip does not know this pairing key)");
		return CHIP_E_SESSION;
	}
	c->session_slot = key->slot;
	return CHIP_OK;
}

/* the 32-bit word of a configuration object, or ~0 when the file is missing */
static uint32_t cfg_word(const chip_t *c, const char *file, const char *name)
{
	char p[420], line[160];
	FILE *f;
	snprintf(p, sizeof p, "%s/%s", c->dir, file);
	f = fopen(p, "r");
	if (!f) return 0xffffffffu;
	while (fgets(line, sizeof line, f)) {
		char nm[CHIP_NAME_LEN];
		unsigned v;
		if (sscanf(line, "%47s 0x%x", nm, &v) == 2 && strcmp(nm, name) == 0) {
			fclose(f);
			return v;
		}
	}
	fclose(f);
	return 0xffffffffu;
}

/* Access objects: see uap.h. The layout comes from libtropic's own example; it has not been confirmed by changing a
 * configuration on a chip, so it stays [assumed] until a spare-chip run. */
static chip_status_t allowed_t(const chip_t *c, const char *uap, int target, char *err)
{
	uint32_t w;
	if (c->session_slot < 0) {
		setmsg(err, "LT_HOST_NO_SESSION");
		return CHIP_E_SESSION;
	}
	w = cfg_word(c, "rconfig", uap);
	/* [measured layout, from libtropic's hardware-wallet example] a byte per group of targets, one bit per pairing key,
	 * a set bit grants access (see uap.h) */
	if (uap_allows(w, uap_kind(uap), c->session_slot, target)) return CHIP_OK;
	setmsg(err, "LT_L3_UNAUTHORIZED");
	return CHIP_E_DENIED;
}
static chip_status_t allowed(const chip_t *c, const char *uap, char *err) { return allowed_t(c, uap, 0, err); }

chip_status_t chip_chip_id(chip_t *c, chip_id_t *out, char *err)
{
	(void)c;
	(void)err;
	memset(out, 0, sizeof *out);
	out->chip_id_ver[3] = 1;
	snprintf(out->text, sizeof out->text,
	         "CHIP_ID ver            = 0x00000001 (v0.0.0.1)\n"
	         "Silicon rev            = 0x00000000 (N/A)\n"
	         "S/N                    = 0x00000000000000000000000000000000\n"
	         "P/N (long)             = 0x46414B4546414B4546414B4546414B45 (FAKEFAKEFAKEFAKE)\n"
	         "Batch ID               = 0x0000000000\n");
	return CHIP_OK;
}

chip_status_t chip_fw_versions(chip_t *c, uint8_t riscv[4], uint8_t spect[4], char *err)
{
	char p[420];
	unsigned r = 0, s = 0;
	FILE *f;
	snprintf(p, sizeof p, "%s/fw", c->dir);
	f = fopen(p, "r");
	if (!f || fscanf(f, "%8x %8x", &r, &s) != 2) {
		if (f) fclose(f);
		setmsg(err, "fake chip has no firmware file");
		return CHIP_E_OTHER;
	}
	fclose(f);
	for (int i = 0; i < 4; i++) {
		riscv[i] = (uint8_t)(r >> (24 - 8 * i));
		spect[i] = (uint8_t)(s >> (24 - 8 * i));
	}
	return CHIP_OK;
}

chip_status_t chip_random(chip_t *c, uint8_t *buf, size_t n, char *err)
{
	FILE *f;
	chip_status_t st = allowed(c, "TR01_CFG_UAP_RANDOM_VALUE_GET", err);
	if (st) return st;
	if (n < 1 || n > 255) {
		setmsg(err, "LT_PARAM_ERR (1 to 255 bytes)");
		return CHIP_E_PARAM;
	}
	f = fopen("/dev/urandom", "rb");
	if (!f || fread(buf, 1, n, f) != n) {
		if (f) fclose(f);
		setmsg(err, "fake chip: no randomness");
		return CHIP_E_OTHER;
	}
	fclose(f);
	return CHIP_OK;
}

chip_status_t chip_mem_read(chip_t *c, unsigned slot, uint8_t *buf, size_t *len, char *err)
{
	char p[420];
	chip_status_t st = allowed(c, "TR01_CFG_UAP_R_MEM_DATA_READ", err);
	if (st) return st;
	if (slot >= CHIP_RMEM_SLOTS) {
		setmsg(err, "LT_PARAM_ERR (R-MEM slots are 0-511)");
		return CHIP_E_PARAM;
	}
	path_of(c, p, sizeof p, "rmem/%u", slot);
	if (read_file(p, buf, CHIP_RMEM_MAX, len) != 0) {
		setmsg(err, "LT_L3_R_MEM_DATA_READ_SLOT_EMPTY");
		return CHIP_E_EMPTY;
	}
	return CHIP_OK;
}

chip_status_t chip_mem_write(chip_t *c, unsigned slot, const uint8_t *buf, size_t len, char *err)
{
	char p[420];
	chip_status_t st = allowed(c, "TR01_CFG_UAP_R_MEM_DATA_WRITE", err);
	if (st) return st;
	if (slot >= CHIP_RMEM_SLOTS || len > CHIP_RMEM_MAX || len == 0) {
		setmsg(err, "LT_PARAM_ERR (slot 0-511, 1 to 475 bytes)");
		return CHIP_E_PARAM;
	}
	path_of(c, p, sizeof p, "rmem/%u", slot);
	/* [assumed] writing a written slot fails until it is erased */
	if (access(p, F_OK) == 0) {
		setmsg(err, "LT_L3_R_MEM_DATA_WRITE_WRITE_FAIL (slot already written)");
		return CHIP_E_OCCUPIED;
	}
	if (getenv("TROPICCTL_FAKE_CORRUPT_WRITE")) { /* stored, but not what was written */
		uint8_t bad[CHIP_RMEM_MAX];
		memcpy(bad, buf, len);
		bad[0] ^= 0xff;
		if (write_file(p, bad, len) == 0) return CHIP_OK;
	}
	if (write_file(p, buf, len) != 0) {
		setmsg(err, "fake chip: cannot store the slot");
		return CHIP_E_OTHER;
	}
	return CHIP_OK;
}

chip_status_t chip_mem_erase(chip_t *c, unsigned slot, char *err)
{
	char p[420];
	chip_status_t st = allowed(c, "TR01_CFG_UAP_R_MEM_DATA_ERASE", err);
	if (st) return st;
	if (slot >= CHIP_RMEM_SLOTS) {
		setmsg(err, "LT_PARAM_ERR (R-MEM slots are 0-511)");
		return CHIP_E_PARAM;
	}
	path_of(c, p, sizeof p, "rmem/%u", slot);
	if (!getenv("TROPICCTL_FAKE_SILENT_ERASE")) unlink(p); /* erasing an empty slot is not an error on the chip */
	return CHIP_OK;
}

chip_status_t chip_pairing_read(chip_t *c, unsigned slot, uint8_t pub[32], char *err)
{
	char p[420];
	size_t n = 0;
	chip_status_t st = slot < CHIP_PAIRING_SLOTS ? allowed_t(c, "TR01_CFG_UAP_PAIRING_KEY_READ", (int)slot, err) : CHIP_OK;
	if (st) return st;
	if (slot >= CHIP_PAIRING_SLOTS) {
		setmsg(err, "LT_PARAM_ERR (pairing slots are 0-3)");
		return CHIP_E_PARAM;
	}
	snprintf(p, sizeof p, "%s/pairing/%u.invalid", c->dir, slot);
	if (access(p, F_OK) == 0) {
		setmsg(err, "LT_L3_SLOT_INVALID");
		return CHIP_E_INVALID;
	}
	path_of(c, p, sizeof p, "pairing/%u", slot);
	if (read_file(p, pub, 32, &n) != 0 || n != 32) {
		setmsg(err, "LT_L3_SLOT_EMPTY");
		return CHIP_E_EMPTY;
	}
	return CHIP_OK;
}

chip_status_t chip_config_dump(chip_t *c, chip_config_obj_t objs[CHIP_CONFIG_OBJ_CNT], char *err)
{
	chip_status_t st = allowed(c, "TR01_CFG_UAP_R_CONFIG_READ", err);
	if (st) return st;
	st = allowed(c, "TR01_CFG_UAP_I_CONFIG_READ", err);
	if (st) return st;
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) {
		snprintf(objs[i].name, CHIP_NAME_LEN, "%s", CFG_NAMES[i]);
		objs[i].r_value = cfg_word(c, "rconfig", CFG_NAMES[i]);
		objs[i].i_value = cfg_word(c, "iconfig", CFG_NAMES[i]);
	}
	return CHIP_OK;
}

/* ------------------------------------------------------------------------------------------------------------ ECC */

static int slot_ok(unsigned slot, char *err, unsigned limit, const char *what)
{
	if (slot < limit) return 1;
	snprintf(err, CHIP_ERRLEN, "LT_PARAM_ERR (%s)", what);
	return 0;
}

chip_status_t chip_ecc_read(chip_t *c, unsigned slot, uint8_t pub[64], size_t *publen, chip_curve_t *curve,
                            chip_origin_t *origin, char *err)
{
	char p[420], word[16];
	uint8_t priv[32];
	size_t n = 0;
	FILE *f;
	chip_status_t st;
	if (!slot_ok(slot, err, CHIP_ECC_SLOTS, "ECC slots are 0-31")) return CHIP_E_PARAM;
	st = allowed_t(c, "TR01_CFG_UAP_ECC_KEY_READ", (int)slot, err);
	if (st) return st;
	snprintf(p, sizeof p, "%s/ecc/%u.key", c->dir, slot);
	if (read_file(p, priv, 32, &n) != 0 || n != 32) {
		setmsg(err, "LT_L3_INVALID_KEY (no key in this slot)");
		return CHIP_E_EMPTY;
	}
	snprintf(p, sizeof p, "%s/ecc/%u.curve", c->dir, slot);
	f = fopen(p, "r");
	*curve = (f && fscanf(f, "%15s", word) == 1 && strcmp(word, "ed25519") == 0) ? CHIP_CURVE_ED25519 : CHIP_CURVE_P256;
	if (f) fclose(f);
	snprintf(p, sizeof p, "%s/ecc/%u.origin", c->dir, slot);
	f = fopen(p, "r");
	*origin = (f && fscanf(f, "%15s", word) == 1 && strcmp(word, "stored") == 0) ? CHIP_ORIGIN_STORED : CHIP_ORIGIN_GENERATED;
	if (f) fclose(f);
	if (ecc_host_public(*curve, priv, pub, publen) != 0) {
		setmsg(err, "fake chip: bad key in slot");
		return CHIP_E_OTHER;
	}
	memset(priv, 0, sizeof priv);
	return CHIP_OK;
}

static chip_status_t ecc_put(chip_t *c, unsigned slot, chip_curve_t curve, const uint8_t priv[32], const char *origin)
{
	char p[420];
	FILE *f;
	snprintf(p, sizeof p, "%s/ecc", c->dir);
	mkdir(p, 0700);
	snprintf(p, sizeof p, "%s/ecc/%u.key", c->dir, slot);
	if (write_file(p, priv, 32) != 0) return CHIP_E_OTHER;
	snprintf(p, sizeof p, "%s/ecc/%u.curve", c->dir, slot);
	f = fopen(p, "w");
	if (!f) return CHIP_E_OTHER;
	fputs(curve == CHIP_CURVE_ED25519 ? "ed25519\n" : "p256\n", f);
	fclose(f);
	snprintf(p, sizeof p, "%s/ecc/%u.origin", c->dir, slot);
	f = fopen(p, "w");
	if (!f) return CHIP_E_OTHER;
	fprintf(f, "%s\n", origin);
	fclose(f);
	return CHIP_OK;
}

static int ecc_occupied(const chip_t *c, unsigned slot)
{
	char p[420];
	snprintf(p, sizeof p, "%s/ecc/%u.key", c->dir, slot);
	return access(p, F_OK) == 0;
}

chip_status_t chip_ecc_generate(chip_t *c, unsigned slot, chip_curve_t curve, char *err)
{
	uint8_t priv[32];
	chip_status_t st;
	if (!slot_ok(slot, err, CHIP_ECC_SLOTS, "ECC slots are 0-31")) return CHIP_E_PARAM;
	st = allowed_t(c, "TR01_CFG_UAP_ECC_KEY_GENERATE", (int)slot, err);
	if (st) return st;
	if (ecc_occupied(c, slot)) {
		setmsg(err, "LT_L3_SLOT_NOT_EMPTY");
		return CHIP_E_OCCUPIED;
	}
	if (ecc_host_generate(curve, priv) != 0) {
		setmsg(err, "fake chip: key generation failed");
		return CHIP_E_OTHER;
	}
	st = ecc_put(c, slot, curve, priv, "generated");
	memset(priv, 0, sizeof priv);
	if (st) setmsg(err, "fake chip: cannot store the key");
	return st;
}

chip_status_t chip_ecc_store(chip_t *c, unsigned slot, chip_curve_t curve, const uint8_t priv[32], char *err)
{
	uint8_t pub[64];
	size_t pl;
	chip_status_t st;
	if (!slot_ok(slot, err, CHIP_ECC_SLOTS, "ECC slots are 0-31")) return CHIP_E_PARAM;
	st = allowed_t(c, "TR01_CFG_UAP_ECC_KEY_STORE", (int)slot, err);
	if (st) return st;
	if (ecc_occupied(c, slot)) {
		setmsg(err, "LT_L3_SLOT_NOT_EMPTY");
		return CHIP_E_OCCUPIED;
	}
	if (ecc_host_public(curve, priv, pub, &pl) != 0) {
		setmsg(err, "LT_L3_INVALID_KEY (not a valid private key for this curve)");
		return CHIP_E_PARAM;
	}
	st = ecc_put(c, slot, curve, priv, "stored");
	if (st) setmsg(err, "fake chip: cannot store the key");
	return st;
}

chip_status_t chip_ecc_erase(chip_t *c, unsigned slot, char *err)
{
	char p[420];
	chip_status_t st;
	if (!slot_ok(slot, err, CHIP_ECC_SLOTS, "ECC slots are 0-31")) return CHIP_E_PARAM;
	st = allowed_t(c, "TR01_CFG_UAP_ECC_KEY_ERASE", (int)slot, err);
	if (st) return st;
	snprintf(p, sizeof p, "%s/ecc/%u.key", c->dir, slot);
	unlink(p);
	snprintf(p, sizeof p, "%s/ecc/%u.curve", c->dir, slot);
	unlink(p);
	snprintf(p, sizeof p, "%s/ecc/%u.origin", c->dir, slot);
	unlink(p);
	return CHIP_OK;
}

static chip_status_t load_priv(const chip_t *c, unsigned slot, uint8_t priv[32], chip_curve_t *curve, char *err)
{
	char p[420], word[16];
	size_t n = 0;
	FILE *f;
	snprintf(p, sizeof p, "%s/ecc/%u.key", c->dir, slot);
	if (read_file(p, priv, 32, &n) != 0 || n != 32) {
		setmsg(err, "LT_L3_INVALID_KEY (no key in this slot)");
		return CHIP_E_EMPTY;
	}
	snprintf(p, sizeof p, "%s/ecc/%u.curve", c->dir, slot);
	f = fopen(p, "r");
	*curve = (f && fscanf(f, "%15s", word) == 1 && strcmp(word, "ed25519") == 0) ? CHIP_CURVE_ED25519 : CHIP_CURVE_P256;
	if (f) fclose(f);
	return CHIP_OK;
}

chip_status_t chip_ecdsa_sign(chip_t *c, unsigned slot, const uint8_t hash[32], uint8_t sig[64], char *err)
{
	uint8_t priv[32];
	chip_curve_t curve;
	chip_status_t st;
	if (!slot_ok(slot, err, CHIP_ECC_SLOTS, "ECC slots are 0-31")) return CHIP_E_PARAM;
	st = allowed_t(c, "TR01_CFG_UAP_ECDSA_SIGN", (int)slot, err);
	if (st) return st;
	st = load_priv(c, slot, priv, &curve, err);
	if (st) return st;
	if (curve != CHIP_CURVE_P256) {
		setmsg(err, "LT_L3_INVALID_KEY (the slot holds an Ed25519 key; ECDSA needs P-256)");
		return CHIP_E_PARAM;
	}
	if (ecc_host_ecdsa(priv, hash, sig) != 0) {
		setmsg(err, "fake chip: signing failed");
		return CHIP_E_OTHER;
	}
	memset(priv, 0, sizeof priv);
	return CHIP_OK;
}

chip_status_t chip_eddsa_sign(chip_t *c, unsigned slot, const uint8_t *msg, size_t len, uint8_t sig[64], char *err)
{
	uint8_t priv[32];
	chip_curve_t curve;
	chip_status_t st;
	if (!slot_ok(slot, err, CHIP_ECC_SLOTS, "ECC slots are 0-31")) return CHIP_E_PARAM;
	st = allowed_t(c, "TR01_CFG_UAP_EDDSA_SIGN", (int)slot, err);
	if (st) return st;
	st = load_priv(c, slot, priv, &curve, err);
	if (st) return st;
	if (curve != CHIP_CURVE_ED25519) {
		setmsg(err, "LT_L3_INVALID_KEY (the slot holds a P-256 key; EdDSA needs Ed25519)");
		return CHIP_E_PARAM;
	}
	if (ecc_host_eddsa(priv, msg, len, sig) != 0) {
		setmsg(err, "fake chip: signing failed");
		return CHIP_E_OTHER;
	}
	memset(priv, 0, sizeof priv);
	return CHIP_OK;
}

/* ------------------------------------------------------------------------------------------- pairing, config */

chip_status_t chip_pairing_write(chip_t *c, unsigned slot, const uint8_t pub[32], char *err)
{
	char p[420];
	uint8_t shown[32];
	chip_status_t st;
	if (slot < 1 || slot >= CHIP_PAIRING_SLOTS) {
		setmsg(err, "LT_PARAM_ERR (writable pairing slots are 1-3)");
		return CHIP_E_PARAM;
	}
	st = allowed_t(c, "TR01_CFG_UAP_PAIRING_KEY_WRITE", (int)slot, err);
	if (st) return st;
	snprintf(p, sizeof p, "%s/pairing/%u.invalid", c->dir, slot);
	if (access(p, F_OK) == 0) {
		setmsg(err, "LT_L3_SLOT_INVALID");
		return CHIP_E_INVALID;
	}
	path_of(c, p, sizeof p, "pairing/%u", slot);
	if (access(p, F_OK) == 0) {
		setmsg(err, "LT_L3_SLOT_NOT_EMPTY");
		return CHIP_E_OCCUPIED;
	}
	if (getenv("TROPICCTL_FAKE_PAIRING_SILENT")) return CHIP_OK; /* [assumed] firmware older than 2.0.0 outside the temperature range */
	memcpy(shown, pub, 32);
	if (getenv("TROPICCTL_FAKE_PAIRING_CORRUPT")) shown[0] ^= 0xff;
	return write_file(p, shown, 32) == 0 ? CHIP_OK : CHIP_E_OTHER;
}

chip_status_t chip_pairing_invalidate(chip_t *c, unsigned slot, char *err)
{
	char p[420];
	FILE *f;
	chip_status_t st;
	if (slot >= CHIP_PAIRING_SLOTS) {
		setmsg(err, "LT_PARAM_ERR (pairing slots are 0-3)");
		return CHIP_E_PARAM;
	}
	st = allowed_t(c, "TR01_CFG_UAP_PAIRING_KEY_INVALIDATE", (int)slot, err);
	if (st) return st;
	snprintf(p, sizeof p, "%s/pairing/%u.invalid", c->dir, slot);
	f = fopen(p, "w");
	if (!f) return CHIP_E_OTHER;
	fclose(f);
	return CHIP_OK;
}

static void write_cfg(const chip_t *c, const char *file, const uint32_t w[CHIP_CONFIG_OBJ_CNT])
{
	char p[420];
	FILE *f;
	snprintf(p, sizeof p, "%s/%s", c->dir, file);
	f = fopen(p, "w");
	if (!f) return;
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) fprintf(f, "%s 0x%08x\n", CFG_NAMES[i], w[i]);
	fclose(f);
}

chip_status_t chip_rconfig_rewrite(chip_t *c, const uint32_t words[CHIP_CONFIG_OBJ_CNT], int *stage, char *err)
{
	uint32_t defaults[CHIP_CONFIG_OBJ_CNT];
	chip_status_t st = allowed(c, "TR01_CFG_UAP_R_CONFIG_WRITE_ERASE", err);
	*stage = 0;
	if (st) return st;
	/* erase first, always: this is the only place that writes the R-config */
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) defaults[i] = i == 0 ? 0xffffffefu : 0xffffffffu;
	write_cfg(c, "rconfig", defaults);
	*stage = 1;
	if (getenv("TROPICCTL_FAKE_CONFIG_FAIL_AFTER_ERASE")) {
		setmsg(err, "LT_L3_FAIL (the R-config write failed after the erase)");
		return CHIP_E_OTHER;
	}
	if (getenv("TROPICCTL_FAKE_CONFIG_PARTIAL")) {
		uint32_t part[CHIP_CONFIG_OBJ_CNT];
		memcpy(part, words, sizeof part);
		part[CHIP_CONFIG_OBJ_CNT - 1] = defaults[CHIP_CONFIG_OBJ_CNT - 1];
		write_cfg(c, "rconfig", part);
	} else {
		write_cfg(c, "rconfig", words);
	}
	*stage = 2;
	return CHIP_OK;
}

chip_status_t chip_iconfig_clear_bit(chip_t *c, int object, int bit, char *err)
{
	uint32_t w[CHIP_CONFIG_OBJ_CNT];
	chip_status_t st = allowed(c, "TR01_CFG_UAP_I_CONFIG_WRITE", err);
	if (st) return st;
	if (object < 0 || object >= CHIP_CONFIG_OBJ_CNT || bit < 0 || bit > 31) {
		setmsg(err, "LT_PARAM_ERR");
		return CHIP_E_PARAM;
	}
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++) w[i] = cfg_word(c, "iconfig", CFG_NAMES[i]);
	if (!getenv("TROPICCTL_FAKE_ICONFIG_SILENT")) w[object] &= ~(1u << bit); /* [measured rule: one way, 1 to 0] */
	write_cfg(c, "iconfig", w);
	return CHIP_OK;
}

/* ------------------------------------------------------------------------------ MAC-and-Destroy and counters */

/* [assumed] The chip's MAC-and-Destroy sequence, from the application note: V1 = the slot's old content, the slot is
 * overwritten with V2 = F1(v_in || i), and the answer is F2(V1 || V2 || i), where F1 and F2 are keyed with values
 * only the chip knows (the fake keeps them in a file). */
chip_status_t chip_mnd(chip_t *c, unsigned slot, const uint8_t out[32], uint8_t in[32], char *err)
{
	char p[420];
	uint8_t keys[64], v1[32] = {0}, v2[32], buf[65], idx = (uint8_t)slot;
	size_t n = 0;
	chip_status_t st;
	if (getenv("TROPICCTL_FAKE_MND_FAIL")) {
		setmsg(err, "LT_L3_FAIL (fake MAC-and-Destroy failure)");
		return CHIP_E_OTHER;
	}
	if (slot >= CHIP_MND_SLOTS) {
		setmsg(err, "LT_PARAM_ERR (MAC-and-Destroy slots are 0-127)");
		return CHIP_E_PARAM;
	}
	st = allowed_t(c, "TR01_CFG_UAP_MAC_AND_DESTROY", (int)slot, err);
	if (st) return st;
	snprintf(p, sizeof p, "%s/mnd_keys", c->dir);
	if (read_file(p, keys, 64, &n) != 0 || n != 64) {
		if (crypto_random(keys, 64) != 0 || write_file(p, keys, 64) != 0) {
			setmsg(err, "fake chip: no MAC-and-Destroy keys");
			return CHIP_E_OTHER;
		}
	}
	snprintf(p, sizeof p, "%s/mnd", c->dir);
	mkdir(p, 0700);
	snprintf(p, sizeof p, "%s/mnd/%u", c->dir, slot);
	if (read_file(p, v1, 32, &n) != 0 || n != 32) memset(v1, 0, 32); /* [assumed] an unwritten slot reads as zeros */
	memcpy(buf, out, 32);
	buf[32] = idx;
	crypto_hmac_sha256(keys, 32, buf, 33, v2);
	if (write_file(p, v2, 32) != 0) {
		setmsg(err, "fake chip: cannot store the slot");
		return CHIP_E_OTHER;
	}
	memcpy(buf, v1, 32);
	memcpy(buf + 32, v2, 32);
	buf[64] = idx;
	crypto_hmac_sha256(keys + 32, 32, buf, 65, in);
	return CHIP_OK;
}

static int counter_path(const chip_t *c, unsigned n, char *p, size_t pl)
{
	char d[420];
	snprintf(d, sizeof d, "%s/counters", c->dir);
	mkdir(d, 0700);
	snprintf(p, pl, "%s/counters/%u", c->dir, n);
	return 0;
}

chip_status_t chip_counter_init(chip_t *c, unsigned n, uint32_t value, char *err)
{
	char p[420];
	FILE *f;
	chip_status_t st;
	if (n >= CHIP_MCOUNTERS || value > CHIP_MCOUNTER_MAX) {
		setmsg(err, "LT_PARAM_ERR");
		return CHIP_E_PARAM;
	}
	st = allowed_t(c, "TR01_CFG_UAP_MCOUNTER_INIT", (int)n, err);
	if (st) return st;
	counter_path(c, n, p, sizeof p);
	f = fopen(p, "w"); /* [measured in libtropic's functional test] init may be repeated */
	if (!f) return CHIP_E_OTHER;
	fprintf(f, "%u\n", value);
	fclose(f);
	return CHIP_OK;
}

static chip_status_t counter_read(const chip_t *c, unsigned n, uint32_t *v, char *err)
{
	char p[420];
	FILE *f;
	unsigned x;
	counter_path(c, n, p, sizeof p);
	f = fopen(p, "r");
	if (!f || fscanf(f, "%u", &x) != 1) {
		if (f) fclose(f);
		setmsg(err, "LT_L3_COUNTER_INVALID (the counter was never initialised)"); /* [measured 2026-10-11, bench chip] */
		return CHIP_E_EMPTY;
	}
	fclose(f);
	*v = x;
	return CHIP_OK;
}

chip_status_t chip_counter_get(chip_t *c, unsigned n, uint32_t *value, char *err)
{
	chip_status_t st;
	if (n >= CHIP_MCOUNTERS) {
		setmsg(err, "LT_PARAM_ERR");
		return CHIP_E_PARAM;
	}
	st = allowed_t(c, "TR01_CFG_UAP_MCOUNTER_GET", (int)n, err);
	if (st) return st;
	return counter_read(c, n, value, err);
}

chip_status_t chip_counter_update(chip_t *c, unsigned n, char *err)
{
	char p[420];
	uint32_t v;
	FILE *f;
	chip_status_t st;
	if (n >= CHIP_MCOUNTERS) {
		setmsg(err, "LT_PARAM_ERR");
		return CHIP_E_PARAM;
	}
	st = allowed_t(c, "TR01_CFG_UAP_MCOUNTER_UPDATE", (int)n, err);
	if (st) return st;
	st = counter_read(c, n, &v, err);
	if (st) return st;
	if (v == 0) {
		setmsg(err, "LT_L3_UPDATE_ERR (the counter is at zero)"); /* [measured in libtropic's functional test] */
		return CHIP_E_OTHER;
	}
	counter_path(c, n, p, sizeof p);
	f = fopen(p, "w");
	if (!f) return CHIP_E_OTHER;
	fprintf(f, "%u\n", v - 1); /* [measured in libtropic's functional test] an update decrements by one */
	fclose(f);
	return CHIP_OK;
}

chip_status_t chip_mode(chip_t *c, chip_mode_t *mode, char *err)
{
	(void)c;
	(void)err;
	*mode = CHIP_MODE_APPLICATION;
	return CHIP_OK;
}

chip_status_t chip_certs(chip_t *c, uint8_t certs[CHIP_CERTS][CHIP_CERT_MAX], size_t len[CHIP_CERTS], char *err)
{
	(void)err;
	for (int i = 0; i < CHIP_CERTS; i++) {
		char p[420];
		snprintf(p, sizeof p, "%s/certs/%d.der", c->dir, i);
		len[i] = 0;
		if (read_file(p, certs[i], CHIP_CERT_MAX, &len[i]) != 0) len[i] = 0;
	}
	return CHIP_OK;
}
