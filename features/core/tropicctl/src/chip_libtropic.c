/*
 * chip_libtropic.c -- the real chip, through libtropic 4.x with the OpenSSL crypto layer and the Linux SPI/GPIO
 * hardware layer. This is the only file that includes libtropic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chip.h"
#include "libtropic.h"
#include "libtropic_openssl.h"
#include "libtropic_port_linux_spi.h"

struct chip {
	lt_handle_t h;
	lt_dev_linux_spi_t dev;
	lt_ctx_openssl_t ctx;
};

static chip_status_t map(lt_ret_t r, char *err)
{
	if (r == LT_OK) return CHIP_OK;
	snprintf(err, CHIP_ERRLEN, "%s", lt_ret_verbose(r));
	switch (r) {
	case LT_HAL_ERROR:
	case LT_L2_NO_RESP: return CHIP_E_UNREACHABLE;
	case LT_L2_HSK_ERR:
	case LT_L2_NO_SESSION:
	case LT_HOST_NO_SESSION: return CHIP_E_SESSION;
	case LT_L3_UNAUTHORIZED: return CHIP_E_DENIED;
	case LT_L3_SLOT_EMPTY:
	case LT_L3_R_MEM_DATA_READ_SLOT_EMPTY: return CHIP_E_EMPTY;
	case LT_L3_SLOT_INVALID: return CHIP_E_INVALID;
	case LT_L3_SLOT_NOT_EMPTY: return CHIP_E_OCCUPIED;
	case LT_PARAM_ERR: return CHIP_E_PARAM;
	default: return CHIP_E_OTHER;
	}
}

chip_status_t chip_open(const chip_cfg_t *cfg, chip_t **out, char *err)
{
	chip_t *c = calloc(1, sizeof *c);
	lt_ret_t r;
	if (!c) {
		snprintf(err, CHIP_ERRLEN, "out of memory");
		return CHIP_E_OTHER;
	}
	snprintf(c->dev.spi_dev, sizeof c->dev.spi_dev, "%s", cfg->spi_dev);
	snprintf(c->dev.gpio_dev, sizeof c->dev.gpio_dev, "%s", cfg->gpio_dev);
	c->dev.spi_speed = cfg->speed;
	c->dev.gpio_cs_num = cfg->cs_pin;
	/* libtropic 4.x: the application owns the crypto context and hands it to the handle */
	c->h.l3.crypto_ctx = &c->ctx;
	c->h.l2.device = &c->dev;
	r = lt_init(&c->h);
	if (r != LT_OK) {
		chip_status_t st = map(r, err);
		free(c);
		return st;
	}
	*out = c;
	return CHIP_OK;
}

void chip_close(chip_t *c)
{
	if (!c) return;
	lt_deinit(&c->h);
	memset(c, 0, sizeof *c);
	free(c);
}

void chip_shared_key(pairing_key_t *out)
{
	memcpy(out->priv, lt_sh0priv_eng_sample, 32);
	memcpy(out->pub, lt_sh0pub_eng_sample, 32);
	out->slot = TR01_PAIRING_KEY_SLOT_INDEX_0;
}

chip_status_t chip_session(chip_t *c, const pairing_key_t *key, char *err)
{
	return map(lt_verify_chip_and_start_secure_session(&c->h, key->priv, key->pub, (lt_pkey_index_t)key->slot), err);
}

static char g_cap[4096];
static size_t g_caplen;
static int cap_printf(const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(g_cap + g_caplen, sizeof g_cap - g_caplen, fmt, ap);
	va_end(ap);
	if (n > 0) g_caplen += (size_t)n < sizeof g_cap - g_caplen ? (size_t)n : sizeof g_cap - g_caplen - 1;
	return n;
}

chip_status_t chip_chip_id(chip_t *c, chip_id_t *out, char *err)
{
	struct lt_chip_id_t id;
	chip_status_t st = map(lt_get_info_chip_id(&c->h, &id), err);
	if (st != CHIP_OK) return st;
	memset(out, 0, sizeof *out);
#define CP(f) memcpy(out->f, id.f, sizeof out->f)
	CP(chip_id_ver); CP(fl_chip_info); CP(func_test_info); CP(silicon_rev); CP(packg_type_id); CP(prov_ver_fab_id_pn);
	CP(provisioning_date); CP(hsm_ver); CP(prog_ver); CP(part_num_data); CP(prov_templ_ver); CP(prov_templ_tag);
	CP(prov_spec_ver); CP(prov_spec_tag); CP(batch_id);
#undef CP
	g_caplen = 0;
	g_cap[0] = '\0';
	st = map(lt_print_chip_id(&id, cap_printf), err);
	if (st != CHIP_OK) return st;
	snprintf(out->text, sizeof out->text, "%s", g_cap);
	return CHIP_OK;
}

chip_status_t chip_fw_versions(chip_t *c, uint8_t riscv[4], uint8_t spect[4], char *err)
{
	chip_status_t st = map(lt_get_info_riscv_fw_ver(&c->h, riscv), err);
	if (st != CHIP_OK) return st;
	return map(lt_get_info_spect_fw_ver(&c->h, spect), err);
}

chip_status_t chip_random(chip_t *c, uint8_t *buf, size_t n, char *err)
{
	return map(lt_random_value_get(&c->h, buf, (uint8_t)n), err);
}

chip_status_t chip_mem_read(chip_t *c, unsigned slot, uint8_t *buf, size_t *len, char *err)
{
	uint16_t got = 0;
	chip_status_t st = map(lt_r_mem_data_read(&c->h, (uint16_t)slot, buf, CHIP_RMEM_MAX, &got), err);
	if (st == CHIP_OK) *len = got;
	return st;
}

chip_status_t chip_mem_write(chip_t *c, unsigned slot, const uint8_t *buf, size_t len, char *err)
{
	return map(lt_r_mem_data_write(&c->h, (uint16_t)slot, buf, (uint16_t)len), err);
}

chip_status_t chip_mem_erase(chip_t *c, unsigned slot, char *err)
{
	return map(lt_r_mem_data_erase(&c->h, (uint16_t)slot), err);
}

chip_status_t chip_pairing_read(chip_t *c, unsigned slot, uint8_t pub[32], char *err)
{
	return map(lt_pairing_key_read(&c->h, pub, (lt_pkey_index_t)slot), err);
}

chip_status_t chip_config_dump(chip_t *c, chip_config_obj_t objs[CHIP_CONFIG_OBJ_CNT], char *err)
{
	struct lt_config_t r = {{0}}, i = {{0}};
	chip_status_t st = map(lt_read_whole_R_config(&c->h, &r), err);
	if (st != CHIP_OK) return st;
	st = map(lt_read_whole_I_config(&c->h, &i), err);
	if (st != CHIP_OK) return st;
	for (int k = 0; k < CHIP_CONFIG_OBJ_CNT; k++) {
		size_t n = strlen(cfg_desc_table[k].desc);
		while (n > 0 && cfg_desc_table[k].desc[n - 1] == ' ') n--;
		if (n >= CHIP_NAME_LEN) n = CHIP_NAME_LEN - 1;
		memcpy(objs[k].name, cfg_desc_table[k].desc, n);
		objs[k].name[n] = '\0';
		objs[k].r_value = r.obj[k];
		objs[k].i_value = i.obj[k];
	}
	return CHIP_OK;
}
