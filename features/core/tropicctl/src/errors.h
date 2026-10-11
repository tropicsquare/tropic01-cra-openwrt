/* errors.h -- exit codes (see SPEC.md section 4.3) and the mapping from chip statuses. */
#ifndef TROPICCTL_ERRORS_H
#define TROPICCTL_ERRORS_H

#include "chip.h"

enum {
	TC_OK = 0,
	TC_INTERNAL = 1,
	TC_USAGE = 2,
	TC_UNREACHABLE = 3,
	TC_SESSION = 4,
	TC_DENIED = 5,
	TC_NOTFOUND = 6,
	TC_OCCUPIED = 7,
	TC_VERIFY = 8,
	TC_REFUSED = 9,
	TC_CHIP = 10
};

int tc_exit_for_status(chip_status_t st);
const char *tc_exit_name(int code);

#endif
