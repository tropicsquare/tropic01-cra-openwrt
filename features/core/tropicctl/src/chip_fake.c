/*
 * chip_fake.c -- a hand-written fake TROPIC01 for tests on a host. State lives in the directory named by
 * $TROPICCTL_FAKE_DIR (created with defaults when $TROPICCTL_FAKE_INIT=1):
 *
 *   pairing/N         32 bytes: the public key held in pairing slot N (missing = empty); pairing/N.invalid = invalidated
 *   rmem/N            content of R-MEM slot N (missing = empty)
 *   rconfig, iconfig  one "NAME 0xVALUE" line per configuration object
 *   fw                "RISCV SPECT" as 8 hex digits each
 *   down              if present, the chip is unreachable
 *
 * Failure modes for tests: TROPICCTL_FAKE_CORRUPT_WRITE=1 stores wrong content on an R-MEM write;
 * TROPICCTL_FAKE_SILENT_ERASE=1 makes an R-MEM erase do nothing while reporting success.
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
	fputs("00020100 00010300\n", f); /* [measured] App FW 2.1.0, SPECT FW 1.3.0 by libtropic 4.1's notes */
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

/* [assumed] an access object lets pairing key N run the command when bit N of the low nibble of its first byte is set.
 * libtropic's headers describe one byte per group of targets with one bit per pairing key; the fake checks the first
 * group only. Replace when the layout is confirmed on a chip. */
static chip_status_t allowed(const chip_t *c, const char *uap, char *err)
{
	uint32_t w;
	if (c->session_slot < 0) {
		setmsg(err, "LT_HOST_NO_SESSION");
		return CHIP_E_SESSION;
	}
	w = cfg_word(c, "rconfig", uap);
	if (((w & 0xffu) >> c->session_slot) & 1u) return CHIP_OK;
	setmsg(err, "LT_L3_UNAUTHORIZED");
	return CHIP_E_DENIED;
}

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
	chip_status_t st = allowed(c, "TR01_CFG_UAP_PAIRING_KEY_READ", err);
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
