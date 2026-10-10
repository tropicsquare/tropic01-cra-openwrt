#include "cmd_table.h"

#include <string.h>

#include "errors.h"
#include "output.h"

const cmd_t g_cmds[] = {
    {"info", "chip", 'R', "", "Print the chip identification",
     "Reads CHIP_ID. No secure session is needed. The text is the layout `lt-util -i` printed; with --json every CHIP_ID "
     "group is a hex string, as libtropic defines the groups, without interpretation.",
     cmd_info_chip},
    {"info", "fw", 'R', "", "Print the RISC-V and SPECT firmware version bytes",
     "Prints the version bytes as the chip reports them. No secure session is needed.", cmd_info_fw},
    {"rng", "get", 'R', "<count> [--out FILE|-] [--hex]", "Random bytes from the chip (1 to 255)",
     "Without --out the bytes are printed as hex. With --out FILE (or - for stdout) they are written raw. Needs a "
     "secure session.",
     cmd_rng_get},
    {"mem", "list", 'R', "[--range A-B]", "Which R-MEM slots hold data, and how much",
     "Reads each slot in the range (default 0-511; every slot is one chip command, so the full range takes a while) and "
     "lists the populated ones.",
     cmd_mem_list},
    {"mem", "read", 'R', "<slot> [--out FILE|-]", "Read an R-MEM slot (0-511)",
     "Without --out the content is printed as hex; with --out it is written raw. An empty slot is exit code 6.",
     cmd_mem_read},
    {"mem", "write", 'W', "<slot> --in FILE|- [--replace]", "Write an R-MEM slot (1 to 475 bytes)",
     "Refuses an occupied slot unless --replace is given (then the slot is erased first). The slot is read back and "
     "compared; a mismatch is exit code 8. With --dry-run nothing is written.",
     cmd_mem_write},
    {"mem", "erase", 'W', "<slot>", "Erase an R-MEM slot",
     "Erases the slot and checks that it reads empty. Erasing an empty slot is not an error. With --dry-run nothing is "
     "written.",
     cmd_mem_erase},
    {"pairing", "list", 'R', "", "State of the four pairing slots",
     "Each slot is populated, empty or invalidated. Reading the slots needs a session; with the shared key this works "
     "unless the chip's access rights say otherwise.",
     cmd_pairing_list},
    {"pairing", "read", 'R', "<slot> [--out FILE]", "Read the public pairing key of a slot (0-3)",
     "Prints the 32-byte public key as hex, or writes it raw with --out. An empty or invalidated slot is exit code 6.",
     cmd_pairing_read},
    {"config", "dump", 'R', "", "Every R-config and I-config object as raw words",
     "Prints \"R NAME 0xVALUE\" lines for the reconfigurable objects and \"I NAME 0xVALUE\" lines for the one-way "
     "objects. Read-only.",
     cmd_config_dump},
    {"session", "test", 'R', "", "Open and close a secure session; say which key was used",
     "Reports the pairing key and slot that opened the session. Exit code 4 if no key works.", cmd_session_test},
};

const size_t g_ncmds = sizeof g_cmds / sizeof g_cmds[0];

const cmd_t *cmd_find(const char *area, const char *verb)
{
	for (size_t i = 0; i < g_ncmds; i++)
		if (strcmp(g_cmds[i].area, area) == 0 && strcmp(g_cmds[i].verb, verb) == 0) return &g_cmds[i];
	return NULL;
}

int cmd_usage(const cmd_t *c, const char *why)
{
	return out_error(TC_USAGE, "%s (usage: %s %s %s)", why, c->area, c->verb, c->usage);
}
