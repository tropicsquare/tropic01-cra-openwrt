#include "uap.h"

#include <string.h>

static const struct { const char *suffix; uap_kind_t kind; } KINDS[] = {
    {"PAIRING_KEY_WRITE", UAP_PAIRING}, {"PAIRING_KEY_READ", UAP_PAIRING}, {"PAIRING_KEY_INVALIDATE", UAP_PAIRING},
    {"R_CONFIG_WRITE_ERASE", UAP_SINGLE}, {"R_CONFIG_READ", UAP_SINGLE}, {"I_CONFIG_WRITE", UAP_SINGLE},
    {"I_CONFIG_READ", UAP_SINGLE}, {"PING", UAP_SINGLE}, {"R_MEM_DATA_WRITE", UAP_SINGLE}, {"R_MEM_DATA_READ", UAP_SINGLE},
    {"R_MEM_DATA_ERASE", UAP_SINGLE}, {"RANDOM_VALUE_GET", UAP_SINGLE}, {"ECC_KEY_GENERATE", UAP_ECC},
    {"ECC_KEY_STORE", UAP_ECC}, {"ECC_KEY_READ", UAP_ECC}, {"ECC_KEY_ERASE", UAP_ECC}, {"ECDSA_SIGN", UAP_ECC},
    {"EDDSA_SIGN", UAP_ECC}, {"MCOUNTER_INIT", UAP_COUNTER}, {"MCOUNTER_GET", UAP_COUNTER},
    {"MCOUNTER_UPDATE", UAP_COUNTER}, {"MAC_AND_DESTROY", UAP_MND},
};

uap_kind_t uap_kind(const char *name)
{
	const char *p = strstr(name, "TR01_CFG_UAP_");
	if (!p) return UAP_NONE;
	p += strlen("TR01_CFG_UAP_");
	for (size_t i = 0; i < sizeof KINDS / sizeof KINDS[0]; i++)
		if (strcmp(p, KINDS[i].suffix) == 0) return KINDS[i].kind;
	return UAP_NONE;
}

int uap_groups(uap_kind_t kind) { return kind == UAP_NONE ? 0 : kind == UAP_SINGLE ? 1 : 4; }

static int group_of(uap_kind_t kind, int target)
{
	switch (kind) {
	case UAP_PAIRING: return target;       /* one byte per pairing slot acted on */
	case UAP_ECC: return target / 8;       /* ECC slots in groups of 8 */
	case UAP_COUNTER: return target / 4;   /* counters in groups of 4 */
	case UAP_MND: return target / 32;      /* MAC-and-Destroy slots in groups of 32 */
	default: return 0;
	}
}

int uap_allows(uint32_t word, uap_kind_t kind, int session, int target)
{
	int g = group_of(kind, target);
	if (kind == UAP_NONE || session < 0 || session > 3 || g < 0 || g > 3) return 0;
	return (int)(((word >> (8 * g)) >> session) & 1u);
}

uint32_t uap_clear_key(uint32_t word, uap_kind_t kind, int key)
{
	for (int g = 0; g < uap_groups(kind); g++) word &= ~((uint32_t)(1u << key) << (8 * g));
	return word;
}

static void keys_text(char *buf, size_t n, unsigned nibble)
{
	size_t o = 0;
	buf[0] = '\0';
	for (int k = 0; k < 4; k++)
		if (nibble & (1u << k)) o += (size_t)snprintf(buf + o, n - o, "%s%d", o ? "," : "", k);
	if (!o) snprintf(buf, n, "no key");
}

void uap_group_label(uap_kind_t kind, int g, char *buf, size_t n)
{
	switch (kind) {
	case UAP_PAIRING: snprintf(buf, n, "on pairing slot %d", g); break;
	case UAP_ECC: snprintf(buf, n, "ECC slots %d-%d", 8 * g, 8 * g + 7); break;
	case UAP_COUNTER: snprintf(buf, n, "counters %d-%d", 4 * g, 4 * g + 3); break;
	case UAP_MND: snprintf(buf, n, "MAC-and-Destroy slots %d-%d", 32 * g, 32 * g + 31); break;
	default: snprintf(buf, n, "all targets"); break;
	}
}

void uap_describe(FILE *f, const char *name, uint32_t word)
{
	uap_kind_t k = uap_kind(name);
	char keys[32], label[48];
	if (k == UAP_NONE) return; /* a plain object: the raw line is all there is */
	for (int g = 0; g < uap_groups(k); g++) {
		keys_text(keys, sizeof keys, (word >> (8 * g)) & 0xfu);
		uap_group_label(k, g, label, sizeof label);
		fprintf(f, "    %s: pairing keys %s\n", label, keys);
	}
}

void uap_describe_diff(FILE *f, const char *name, uint32_t oldw, uint32_t neww)
{
	uap_kind_t k = uap_kind(name);
	char label[48];
	if (k == UAP_NONE) return;
	for (int g = 0; g < uap_groups(k); g++) {
		unsigned o = (oldw >> (8 * g)) & 0xfu, nw = (neww >> (8 * g)) & 0xfu;
		uap_group_label(k, g, label, sizeof label);
		for (int key = 0; key < 4; key++) {
			if ((o ^ nw) & (1u << key)) fprintf(f, "    pairing key %d %s access (%s)\n", key, (nw >> key) & 1u ? "GAINS" : "loses", label);
		}
	}
}

int uap_index(const chip_config_obj_t *o, const char *name)
{
	for (int i = 0; i < CHIP_CONFIG_OBJ_CNT; i++)
		if (strcmp(o[i].name, name) == 0) return i;
	return -1;
}
