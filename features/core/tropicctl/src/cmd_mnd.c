#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd_table.h"
#include "crypto.h"
#include "errors.h"
#include "files.h"
#include "mnd.h"
#include "output.h"
#include "pin.h"
#include "safety.h"

static int read_add(const char *path, uint8_t *add, size_t *len)
{
	*len = 0;
	if (!path) return TC_OK;
	if (file_read(path, add, MND_ADD_MAX, len) != 0) return out_error(TC_USAGE, "cannot read the additional data %s (at most %d bytes)", path, MND_ADD_MAX);
	return TC_OK;
}

static int result_error(mnd_result_t mr, chip_status_t cst, const char *err, int remaining)
{
	switch (mr) {
	case MND_WRONG_PIN: return out_error(TC_VERIFY, "wrong PIN: %d attempt(s) left", remaining);
	case MND_NO_ATTEMPTS: return out_error(TC_REFUSED, "no PIN attempts remain: the key can no longer be released");
	case MND_CHIP: return out_chip_error(cst, "MAC-and-Destroy failed on the chip", err);
	case MND_STATE: return out_error(TC_INTERNAL, "the PIN state could not be used or saved");
	default: return out_error(TC_USAGE, "the PIN must be %d to %d characters and the additional data at most %d bytes", MND_PIN_MIN, MND_PIN_MAX, MND_ADD_MAX);
	}
}

int cmd_mnd_setup(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("mnd", "setup");
	char err[CHIP_ERRLEN] = "", pin[64], again[64];
	const char *state = NULL, *out = NULL, *addf = NULL, *slots = NULL, *base = NULL, *bad;
	uint8_t add[MND_ADD_MAX], k[32];
	size_t addlen;
	long n = 5, b = 0;
	mnd_state_t *st;
	chip_status_t cst = CHIP_OK;
	mnd_result_t mr;
	int rc, yes = args_flag(a, "--yes-setup-mnd"), replace = args_flag(a, "--replace");
	if (args_value(a, "--state", &state) <= 0 || args_value(a, "--out", &out) <= 0) return cmd_usage(c, "--state FILE and --out KEYFILE are required");
	if (args_value(a, "--add-file", &addf) < 0 || args_value(a, "--slots", &slots) < 0 || args_value(a, "--base-slot", &base) < 0) return cmd_usage(c, "an option needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if ((slots && !args_number(slots, 1, MND_SLOTS_MAX, &n)) || (base && !args_number(base, 0, CHIP_MND_SLOTS - 1, &b)) || b + n > CHIP_MND_SLOTS)
		return cmd_usage(c, "--slots is 1 to 127 and --base-slot plus --slots must not pass 128");
	if (!yes && !x->dry_run) return safety_refuse("--yes-setup-mnd", "setting up a PIN overwrites the chip's MAC-and-Destroy slots");
	if (access(state, F_OK) == 0 && !replace) return out_error(TC_OCCUPIED, "%s exists; --replace would overwrite a PIN state (the old PIN stops working)", state);
	if ((rc = read_add(addf, add, &addlen))) return rc;
	if ((rc = pin_read(x->pin_file, x->pin_stdin, "New PIN: ", pin, sizeof pin))) return rc;
	if (!x->pin_file && !x->pin_stdin && isatty(0)) {
		if ((rc = pin_read(NULL, 0, "Repeat the PIN: ", again, sizeof again))) return rc;
		if (strcmp(pin, again) != 0) return out_error(TC_USAGE, "the two PINs differ");
	}
	if ((rc = ctx_session(x))) return rc;
	if (x->dry_run) {
		memset(pin, 0, sizeof pin);
		out_notice("DRY RUN (nothing written): would set up %ld attempt slot(s) from MAC-and-Destroy slot %ld, save the state to %s and the key to %s%s", n, b, state, out, yes ? "" : "; needs --yes-setup-mnd");
		return TC_OK;
	}
	st = calloc(1, sizeof *st);
	if (!st) return out_error(TC_INTERNAL, "out of memory");
	mr = mnd_setup(x->chip, pin, add, addlen, (int)n, (int)b, st, k, &cst, err);
	memset(pin, 0, sizeof pin);
	if (mr != MND_OK) {
		rc = result_error(mr, cst, err, 0);
		free(st);
		return rc;
	}
	if (mnd_state_save(state, st) != 0 || file_write(out, k, 32) != 0) {
		memset(k, 0, sizeof k);
		free(st);
		return out_error(TC_INTERNAL, "the chip slots were initialised but the state (%s) or the key file (%s) could not be written", state, out);
	}
	memset(k, 0, sizeof k);
	if (g_json) printf("{\"state\":\"%s\",\"attempts\":%ld,\"base_slot\":%ld}\n", state, n, b);
	else printf("PIN set up: %ld attempt(s) on MAC-and-Destroy slots %ld-%ld; state in %s, key in %s\n", n, b, b + n - 1, state, out);
	free(st);
	return TC_OK;
}

int cmd_mnd_check(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("mnd", "check");
	char err[CHIP_ERRLEN] = "", pin[64];
	const char *state = NULL, *out = NULL, *addf = NULL, *bad;
	uint8_t add[MND_ADD_MAX], k[32];
	size_t addlen;
	mnd_state_t *st;
	chip_status_t cst = CHIP_OK;
	mnd_result_t mr;
	int rc;
	if (args_value(a, "--state", &state) <= 0 || args_value(a, "--out", &out) <= 0) return cmd_usage(c, "--state FILE and --out KEYFILE are required");
	if (args_value(a, "--add-file", &addf) < 0) return cmd_usage(c, "--add-file needs a value");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (a->argc != 0) return cmd_usage(c, "unexpected argument");
	if ((rc = read_add(addf, add, &addlen))) return rc;
	st = calloc(1, sizeof *st);
	if (!st) return out_error(TC_INTERNAL, "out of memory");
	if (mnd_state_load(state, st) != 0) {
		free(st);
		return out_error(TC_USAGE, "cannot read the PIN state %s", state);
	}
	if (st->remaining == 0) {
		free(st);
		return out_error(TC_REFUSED, "no PIN attempts remain: the key can no longer be released");
	}
	if ((rc = pin_read(x->pin_file, x->pin_stdin, "PIN: ", pin, sizeof pin))) {
		free(st);
		return rc;
	}
	if ((rc = ctx_session(x))) {
		free(st);
		return rc;
	}
	if (x->dry_run) {
		memset(pin, 0, sizeof pin);
		out_notice("DRY RUN (nothing written): %d attempt(s) left; a check uses one", st->remaining);
		free(st);
		return TC_OK;
	}
	mr = mnd_check(x->chip, pin, add, addlen, st, state, k, &cst, err);
	memset(pin, 0, sizeof pin);
	if (mr != MND_OK) {
		rc = result_error(mr, cst, err, st->remaining);
		free(st);
		return rc;
	}
	free(st);
	if (file_write(out, k, 32) != 0) {
		memset(k, 0, sizeof k);
		return out_error(TC_INTERNAL, "cannot write the key to %s", out);
	}
	memset(k, 0, sizeof k);
	if (g_json) printf("{\"released\":true,\"file\":\"%s\"}\n", out);
	else printf("PIN accepted; the 32-byte key was written to %s\n", out);
	return TC_OK;
}

int cmd_mnd_status(ctx_t *x, args_t *a)
{
	const cmd_t *c = cmd_find("mnd", "status");
	const char *state = NULL, *bad;
	mnd_state_t *st;
	(void)x;
	if (args_value(a, "--state", &state) <= 0) return cmd_usage(c, "--state FILE is required");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	st = calloc(1, sizeof *st);
	if (!st || mnd_state_load(state, st) != 0) {
		free(st);
		return out_error(TC_USAGE, "cannot read the PIN state %s", state);
	}
	if (g_json) printf("{\"attempts\":%d,\"remaining\":%d,\"base_slot\":%d}\n", st->n, st->remaining, st->base);
	else printf("PIN state %s: %d of %d attempt(s) left (MAC-and-Destroy slots %d-%d; the chip's own count is authoritative)\n", state, st->remaining, st->n, st->base, st->base + st->n - 1);
	free(st);
	return TC_OK;
}

static int seal_common(args_t *a, const cmd_t *c, int seal)
{
	const char *kf = NULL, *in = NULL, *out = NULL, *bad;
	uint8_t key[33], buf[512], res[512];
	size_t kl = 0, il = 0;
	int n;
	if (args_value(a, "--key-file", &kf) <= 0 || args_value(a, "--in", &in) <= 0 || args_value(a, "--out", &out) <= 0)
		return cmd_usage(c, "--key-file, --in and --out are required");
	if (args_unknown(a, &bad)) return cmd_usage(c, bad);
	if (file_read(kf, key, sizeof key, &kl) != 0 || kl != 32) return out_error(TC_USAGE, "%s must hold exactly 32 bytes (the key released by mnd check)", kf);
	if (file_read(in, buf, seal ? 400 : 428, &il) != 0 || il == 0) return out_error(TC_USAGE, "cannot read %s", in);
	n = seal ? crypto_seal(key, buf, il, res) : crypto_open(key, buf, il, res);
	memset(key, 0, sizeof key);
	if (n < 0) return seal ? out_error(TC_INTERNAL, "sealing failed") : out_error(TC_VERIFY, "the data does not open with this key (wrong key or damaged file)");
	if (file_write(out, res, (size_t)n) != 0) return out_error(TC_INTERNAL, "cannot write %s", out);
	memset(res, 0, sizeof res);
	if (g_json) printf("{\"bytes\":%d,\"file\":\"%s\"}\n", n, out);
	return TC_OK;
}

int cmd_mnd_seal(ctx_t *x, args_t *a) { (void)x; return seal_common(a, cmd_find("mnd", "seal"), 1); }
int cmd_mnd_unseal(ctx_t *x, args_t *a) { (void)x; return seal_common(a, cmd_find("mnd", "unseal"), 0); }
