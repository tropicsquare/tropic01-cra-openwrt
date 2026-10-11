/*
 * chip.h -- the interface between the commands and the TROPIC01 chip.
 *
 * Commands never include libtropic: they use only this header. Two implementations exist, selected at build time:
 *   chip_libtropic.c  the real chip, through libtropic (the only file that includes libtropic.h)
 *   chip_fake.c       a hand-written fake keeping its state in files, for tests on a host without a chip
 *
 * Every function returns a chip_status_t and, on failure, a human-readable message in err (CHIP_ERRLEN bytes).
 */
#ifndef TROPICCTL_CHIP_H
#define TROPICCTL_CHIP_H

#include <stddef.h>
#include <stdint.h>

#define CHIP_ERRLEN 160
#define CHIP_PAIRING_SLOTS 4
#define CHIP_RMEM_SLOTS 512
#define CHIP_RMEM_MAX 475 /* largest R-MEM slot content with libtropic 4.2 (444 on older firmware docs) */
#define CHIP_CONFIG_OBJ_CNT 27
#define CHIP_NAME_LEN 48

typedef enum {
	CHIP_OK = 0,
	CHIP_E_UNREACHABLE, /* SPI or GPIO failure, no answer */
	CHIP_E_SESSION,     /* secure session could not be started (key refused) */
	CHIP_E_DENIED,      /* the chip's access rights forbid the command for this pairing key */
	CHIP_E_EMPTY,       /* slot empty / object not found */
	CHIP_E_INVALID,     /* slot was invalidated */
	CHIP_E_OCCUPIED,    /* slot already written */
	CHIP_E_VERIFY,      /* a read-back did not match */
	CHIP_E_PARAM,       /* argument rejected by the chip or the library */
	CHIP_E_OTHER        /* any other chip status; the message carries it */
} chip_status_t;

typedef struct chip chip_t;

typedef struct {
	char spi_dev[256];
	char gpio_dev[256];
	int cs_pin;
	int speed;
} chip_cfg_t;

typedef struct {
	uint8_t priv[32]; /* X25519 private key; never printed or logged */
	uint8_t pub[32];
	int slot; /* pairing slot 0-3 */
} pairing_key_t;

/* CHIP_ID, kept as the groups libtropic defines, without interpretation. text is the legacy `lt-util -i` layout. */
typedef struct {
	uint8_t chip_id_ver[4], fl_chip_info[16], func_test_info[8], silicon_rev[4], packg_type_id[2],
	    prov_ver_fab_id_pn[4], provisioning_date[2], hsm_ver[4], prog_ver[4], part_num_data[16],
	    prov_templ_ver[2], prov_templ_tag[4], prov_spec_ver[2], prov_spec_tag[4], batch_id[5];
	char text[4096];
} chip_id_t;

typedef struct {
	char name[CHIP_NAME_LEN];
	uint32_t r_value;
	uint32_t i_value; /* meaningful only when i_known */
	int i_known;      /* 0: the key of the session may not read the I-config (UAP_I_CONFIG_READ); the R-config is still valid */
} chip_config_obj_t;

chip_status_t chip_open(const chip_cfg_t *cfg, chip_t **out, char *err);
void chip_close(chip_t *c);

/* The shared engineering-sample pairing key (slot 0). Public knowledge, published with libtropic. */
void chip_shared_key(pairing_key_t *out);
/* The two slot-0 keys libtropic publishes: "es01" (engineering sample, the same as chip_shared_key) and "prod0"
 * (production). Returns 1 and fills out, or 0 for any other name. */
int chip_named_key(const char *name, pairing_key_t *out);
chip_status_t chip_session(chip_t *c, const pairing_key_t *key, char *err);
chip_status_t chip_session_end(chip_t *c, char *err); /* abort the secure session so another key can open one */

/* No session needed */
chip_status_t chip_chip_id(chip_t *c, chip_id_t *out, char *err);
chip_status_t chip_fw_versions(chip_t *c, uint8_t riscv[4], uint8_t spect[4], char *err);

/* Session needed */
chip_status_t chip_random(chip_t *c, uint8_t *buf, size_t n, char *err); /* n <= 255 */
chip_status_t chip_mem_read(chip_t *c, unsigned slot, uint8_t *buf, size_t *len, char *err); /* buf >= CHIP_RMEM_MAX */
chip_status_t chip_mem_write(chip_t *c, unsigned slot, const uint8_t *buf, size_t len, char *err);
chip_status_t chip_mem_erase(chip_t *c, unsigned slot, char *err);
chip_status_t chip_pairing_read(chip_t *c, unsigned slot, uint8_t pub[32], char *err);
chip_status_t chip_config_dump(chip_t *c, chip_config_obj_t objs[CHIP_CONFIG_OBJ_CNT], char *err);

/* ---- phase 2 and later ---- */
#define CHIP_ECC_SLOTS 32
#define CHIP_CERTS 4
#define CHIP_CERT_MAX 700 /* one certificate of the store; the store is 3840 bytes in total */
#define CHIP_MCOUNTERS 16
#define CHIP_MND_SLOTS 128
#define CHIP_MCOUNTER_MAX 0xFFFFFFFEu

typedef enum { CHIP_CURVE_P256 = 1, CHIP_CURVE_ED25519 = 2 } chip_curve_t;
typedef enum { CHIP_ORIGIN_GENERATED = 1, CHIP_ORIGIN_STORED = 2 } chip_origin_t;
typedef enum { CHIP_MODE_UNKNOWN = 0, CHIP_MODE_APPLICATION, CHIP_MODE_MAINTENANCE } chip_mode_t;

/* ECC keys. pub is 64 bytes (X||Y) for P-256 and 32 bytes for Ed25519. An empty slot is CHIP_E_EMPTY. */
chip_status_t chip_ecc_read(chip_t *c, unsigned slot, uint8_t pub[64], size_t *publen, chip_curve_t *curve,
                            chip_origin_t *origin, char *err);
chip_status_t chip_ecc_generate(chip_t *c, unsigned slot, chip_curve_t curve, char *err);
chip_status_t chip_ecc_store(chip_t *c, unsigned slot, chip_curve_t curve, const uint8_t priv[32], char *err);
chip_status_t chip_ecc_erase(chip_t *c, unsigned slot, char *err);
chip_status_t chip_ecdsa_sign(chip_t *c, unsigned slot, const uint8_t hash[32], uint8_t sig[64], char *err);
chip_status_t chip_eddsa_sign(chip_t *c, unsigned slot, const uint8_t *msg, size_t len, uint8_t sig[64], char *err);

/* Pairing keys (the write is one-way: the slot is used up; invalidation is one-way too) */
chip_status_t chip_pairing_write(chip_t *c, unsigned slot, const uint8_t pub[32], char *err);
chip_status_t chip_pairing_invalidate(chip_t *c, unsigned slot, char *err);

/*
 * Rewrite the whole R-config. The safe sequence is enforced HERE and nowhere else may write R-config: erase first, then
 * write every object (writing without erasing can put the chip into permanent Alarm Mode, libtropic erratum
 * OI_TR01_ERR_2026010800). *stage tells the caller how far it got: 0 nothing done, 1 erased, 2 written.
 */
chip_status_t chip_rconfig_rewrite(chip_t *c, const uint32_t words[CHIP_CONFIG_OBJ_CNT], int *stage, char *err);
/* Clear one bit (1 to 0, one-way) of an I-config object, by its position in the dump */
chip_status_t chip_iconfig_clear_bit(chip_t *c, int object, int bit, char *err);

/* One MAC-and-Destroy sequence on a slot: out is sent, in receives the chip's answer (both 32 bytes) */
chip_status_t chip_mnd(chip_t *c, unsigned slot, const uint8_t out[32], uint8_t in[32], char *err);

/* Monotonic counters. libtropic's functional test: init may be repeated, update decrements by one, and fails at zero. */
chip_status_t chip_counter_init(chip_t *c, unsigned n, uint32_t value, char *err);
chip_status_t chip_counter_get(chip_t *c, unsigned n, uint32_t *value, char *err);
chip_status_t chip_counter_update(chip_t *c, unsigned n, char *err);

chip_status_t chip_mode(chip_t *c, chip_mode_t *mode, char *err);

/* Restart the chip (L2 Startup_Req, REBOOT). Ends any secure session. R-config changes are loaded by the chip at start-up,
 * so a rewritten R-config takes effect only after this (or a power cycle). No session needed. */
chip_status_t chip_reboot(chip_t *c, char *err);

/* ---- firmware update ---- */
/* The silicon revision ("ABAB" or "ACAB") libtropic's update helper was built for. The images are different for each. */
const char *chip_update_silicon(void);
/*
 * Update both mutable firmware banks with libtropic's recommended sequence (it reboots the chip into maintenance mode
 * itself, which must be enabled in I-config and R-config). No secure session may be open. cpu_ver and spect_ver are the
 * versions the images carry, in the byte order of chip_fw_versions; the real chip ignores them (the caller reads the
 * versions back), the fake uses them. A chip never accepts an older version.
 */
chip_status_t chip_fw_update(chip_t *c, const uint8_t *cpu, size_t cpulen, const uint8_t cpu_ver[4], const uint8_t *spect,
                             size_t spectlen, const uint8_t spect_ver[4], char *err);
/* the chip's certificate store: up to CHIP_CERTS certificates; unused ones have length 0 */
chip_status_t chip_certs(chip_t *c, uint8_t certs[CHIP_CERTS][CHIP_CERT_MAX], size_t len[CHIP_CERTS], char *err);

#endif
