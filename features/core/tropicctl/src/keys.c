#include "keys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "errors.h"
#include "files.h"
#include "output.h"

const char *keys_dir(void)
{
	const char *d = getenv("TROPICCTL_KEY_DIR");
	return d && *d ? d : "/etc/tropic01";
}

/* 1 ok, 0 unusable */
static int load_pair(const char *privf, const char *pubf, int slot, pairing_key_t *out)
{
	size_t n = 0;
	uint8_t priv[32], pub[32];
	if (file_read(privf, priv, sizeof priv, &n) != 0 || n != 32) return 0;
	if (file_read(pubf, pub, sizeof pub, &n) != 0 || n != 32) {
		memset(priv, 0, sizeof priv);
		return 0;
	}
	memcpy(out->priv, priv, 32);
	memcpy(out->pub, pub, 32);
	out->slot = slot;
	memset(priv, 0, sizeof priv);
	return 1;
}

static int read_slot_file(const char *path, int *slot)
{
	char buf[16] = {0};
	long v;
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	if (!fgets(buf, sizeof buf, f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	buf[strcspn(buf, "\r\n")] = '\0';
	if (!args_number(buf, 0, 3, &v)) return 0;
	*slot = (int)v;
	return 1;
}

static int use_unit(pairing_key_t *out, char *desc, size_t dl, int must_exist)
{
	char privf[300], pubf[300], slotf[300];
	int slot;
	snprintf(privf, sizeof privf, "%s/pairing.key", keys_dir());
	snprintf(pubf, sizeof pubf, "%s/pairing.pub", keys_dir());
	snprintf(slotf, sizeof slotf, "%s/pairing.slot", keys_dir());
	if (access(privf, F_OK) != 0) {
		if (must_exist) return out_error(TC_SESSION, "no per-unit pairing key (%s)", privf);
		out_warn("no per-unit pairing key (%s), using the shared engineering-sample key", privf);
		chip_shared_key(out);
		snprintf(desc, dl, "shared engineering-sample key, slot 0");
		return TC_OK;
	}
	if (!read_slot_file(slotf, &slot) || !load_pair(privf, pubf, slot, out))
		return out_error(TC_SESSION,
		                 "%s exists but the per-unit key pair is unusable (need 32-byte .key and .pub and a slot 0-3 in "
		                 ".slot); not falling back to the shared key",
		                 privf);
	out_notice("using the per-unit pairing key, slot %d", slot);
	snprintf(desc, dl, "per-unit pairing key, slot %d", slot);
	return TC_OK;
}

int keys_select(const char *source, pairing_key_t *out, char *desc, size_t desclen)
{
	if (!source || strcmp(source, "auto") == 0) return use_unit(out, desc, desclen, 0);
	if (strcmp(source, "unit") == 0) return use_unit(out, desc, desclen, 1);
	if (strcmp(source, "shared") == 0) {
		chip_shared_key(out);
		out_notice("using the shared engineering-sample key (slot 0)");
		snprintf(desc, desclen, "shared engineering-sample key, slot 0");
		return TC_OK;
	}
	if (strncmp(source, "file:", 5) == 0) {
		char path[300], pubf[310];
		const char *colon = strrchr(source + 5, ':');
		long slot;
		if (!colon || colon == source + 5 || (size_t)(colon - (source + 5)) >= sizeof path ||
		    !args_number(colon + 1, 0, 3, &slot))
			return out_error(TC_USAGE, "--key file:PATH:SLOT needs a path and a slot 0-3");
		memcpy(path, source + 5, (size_t)(colon - (source + 5)));
		path[colon - (source + 5)] = '\0';
		snprintf(pubf, sizeof pubf, "%s.pub", path);
		if (!load_pair(path, pubf, (int)slot, out))
			return out_error(TC_SESSION, "key file %s (and %s) is missing or not 32 bytes", path, pubf);
		snprintf(desc, desclen, "key file %s, slot %ld", path, slot);
		return TC_OK;
	}
	if (strcmp(source, "es01") == 0 || strcmp(source, "prod0") == 0) {
		chip_named_key(source, out);
		out_notice("using the %s slot-0 key published with libtropic", strcmp(source, "es01") == 0 ? "engineering-sample (ES01)" : "production (prod0)");
		snprintf(desc, desclen, "%s slot-0 key, slot 0", source);
		return TC_OK;
	}
	if (strcmp(source, "pin") == 0)
		return out_error(TC_REFUSED, "--key pin (PIN-protected key) is not available in this version");
	return out_error(TC_USAGE, "unknown key source '%s' (auto, shared, es01, prod0, unit, file:PATH:SLOT)", source);
}
