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
	uint32_t i_value;
} chip_config_obj_t;

chip_status_t chip_open(const chip_cfg_t *cfg, chip_t **out, char *err);
void chip_close(chip_t *c);

/* The shared engineering-sample pairing key (slot 0). Public knowledge, published with libtropic. */
void chip_shared_key(pairing_key_t *out);
chip_status_t chip_session(chip_t *c, const pairing_key_t *key, char *err);

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

#endif
