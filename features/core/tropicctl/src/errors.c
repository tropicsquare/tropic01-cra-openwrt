#include "errors.h"

int tc_exit_for_status(chip_status_t st)
{
	switch (st) {
	case CHIP_OK: return TC_OK;
	case CHIP_E_UNREACHABLE: return TC_UNREACHABLE;
	case CHIP_E_SESSION: return TC_SESSION;
	case CHIP_E_DENIED: return TC_DENIED;
	case CHIP_E_EMPTY:
	case CHIP_E_INVALID: return TC_NOTFOUND;
	case CHIP_E_OCCUPIED: return TC_OCCUPIED;
	case CHIP_E_VERIFY: return TC_VERIFY;
	case CHIP_E_PARAM: return TC_USAGE;
	default: return TC_CHIP;
	}
}

const char *tc_exit_name(int code)
{
	switch (code) {
	case TC_OK: return "ok";
	case TC_INTERNAL: return "internal";
	case TC_USAGE: return "usage";
	case TC_UNREACHABLE: return "unreachable";
	case TC_SESSION: return "session";
	case TC_DENIED: return "denied";
	case TC_NOTFOUND: return "not_found";
	case TC_OCCUPIED: return "occupied";
	case TC_VERIFY: return "verify";
	case TC_REFUSED: return "refused";
	default: return "chip";
	}
}
