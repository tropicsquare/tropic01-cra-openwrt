#include "cmd_table.h"

#include <string.h>

#include "errors.h"
#include "output.h"

const cmd_t g_cmds[] = {
    /* chip, fw */
    {"chip", "reboot", 'W', "", "Restart the chip",
     "Restarts the chip so that a rewritten R-config is loaded. The chip loads its R-config at start-up: after config rewrite "
     "or config restore the new access rights are not in force until this (or a power cycle). Ends any session; no key needed.",
     cmd_chip_reboot},
    {"fw", "status", 'R', "", "Firmware versions, silicon revision and mode",
     "Prints the RISC-V and SPECT firmware versions, the chip's silicon revision, whether this build's update images fit it, "
     "and the chip mode. No secure session is needed.",
     cmd_fw_status},
    {"fw", "update", 'I', "(--dir DIR | --cpu FILE --spect FILE) --yes-update-firmware [--enable-maintenance] [--disable-maintenance] [--backup FILE]", "Update the mutable firmware (both banks)",
     "IRREVERSIBLE: a chip never accepts an older version, and an interrupted or wrong update can leave it unusable. Takes the "
     "signed images as libtropic ships them (TROPIC01_fw_update_files/boot_v_X/fw_v_X_Y_Z; the versions are read from the "
     "file names). Refuses images for another silicon revision, a downgrade, and a chip whose I-config has Maintenance Mode "
     "disabled. --enable-maintenance enables it in the R-config first (backup, erase, write, restart); --disable-maintenance "
     "turns it off again after the update has been read back. Run it from a stable power supply and do not interrupt it.",
     cmd_fw_update},
    {"fw", "maintenance", 'H', "<on|off> --yes-rewrite-r-config [--backup FILE]", "Turn Maintenance Mode on or off in the R-config",
     "Rewrites the R-config START_UP object (backup, erase, write, read back, restart). Maintenance Mode is what a firmware "
     "update needs; leaving it on enlarges the attack surface.",
     cmd_fw_maintenance},
    /* info */
    {"info", "chip", 'R', "", "Print the chip identification",
     "Reads CHIP_ID. No secure session is needed. The text is the layout `lt-util -i` printed; with --json every CHIP_ID "
     "group is a hex string, as libtropic defines the groups, without interpretation.",
     cmd_info_chip},
    {"info", "fw", 'R', "", "Print the RISC-V and SPECT firmware version bytes",
     "Prints the version bytes as the chip reports them and the version they stand for, read as libtropic reads them (byte 3 . "
     "byte 2 . byte 1; byte 0 is not part of the version). No secure session is needed.", cmd_info_fw},
    {"info", "mode", 'R', "", "Print the chip mode (application or maintenance)",
     "No secure session is needed. Most commands need the chip in application mode.", cmd_info_mode},
    {"info", "cert", 'R', "[--out-dir DIR]", "List the chip's certificate store",
     "Prints the length of each of the four store certificates; with --out-dir each is written as certN.der. Verifying "
     "the chain against Tropic Square's root is not done by this command. No secure session is needed.",
     cmd_info_cert},
    /* rng */
    {"rng", "get", 'R', "<count> [--out FILE|-] [--hex]", "Random bytes from the chip (1 to 255)",
     "Without --out the bytes are printed as hex. With --out FILE (or - for stdout) they are written raw. Needs a "
     "secure session.",
     cmd_rng_get},
    /* mem */
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
    /* ecc */
    {"ecc", "list", 'R', "", "The 32 ECC key slots: empty, or curve and origin",
     "Origin is generated (made inside the chip, never exported) or stored (a key that was loaded from outside, which the "
     "chip cannot vouch for).",
     cmd_ecc_list},
    {"ecc", "generate", 'W', "<slot> --curve p256|ed25519 [--replace --yes-erase-ecc-key]", "Generate a key inside the chip",
     "The private key never leaves the chip. An occupied slot is refused; --replace erases it first and needs "
     "--yes-erase-ecc-key, because the old key is lost for good. The key is read back and checked.",
     cmd_ecc_generate},
    {"ecc", "store", 'W', "<slot> --curve p256|ed25519 --in FILE [--replace --yes-erase-ecc-key]", "Store a private key from a file",
     "The file holds the 32-byte private key. The key's origin is then \"stored\", not \"generated\", which matters for "
     "trust. Rules for an occupied slot as for generate.",
     cmd_ecc_store},
    {"ecc", "pub", 'R', "<slot> [--out FILE|-] [--format raw|pem]", "The public key of a slot",
     "raw is 64 bytes X||Y for P-256 and 32 bytes for Ed25519; pem is a SubjectPublicKeyInfo. Without --out it is printed.",
     cmd_ecc_pub},
    {"ecc", "erase", 'I', "<slot> --yes-erase-ecc-key", "Erase an ECC key",
     "The key is destroyed for good (a generated key cannot be recreated). Needs --yes-erase-ecc-key; checks the slot reads "
     "empty afterwards. Erasing an empty slot is not an error.",
     cmd_ecc_erase},
    {"ecc", "sign", 'R', "<slot> --in FILE [--out FILE|-] [--alg ecdsa|eddsa] [--digest] [--der]", "Sign with a key in the chip",
     "ECDSA signs a 32-byte hash: without --digest the input is hashed with SHA-256, with --digest it must be exactly 32 bytes. "
     "EdDSA signs the message (up to 4095 bytes). The algorithm follows the slot's curve; a mismatch is refused. The "
     "signature is 64 raw bytes (r||s), or DER with --der (ECDSA).",
     cmd_ecc_sign},
    /* pairing */
    {"pairing", "list", 'R', "", "State of the four pairing slots",
     "Each slot is populated, empty or invalidated. Reading the slots needs a session; with the shared key this works "
     "unless the chip's access rights say otherwise.",
     cmd_pairing_list},
    {"pairing", "read", 'R', "<slot> [--out FILE]", "Read the public pairing key of a slot (0-3)",
     "Prints the 32-byte public key as hex, or writes it raw with --out. An empty or invalidated slot is exit code 6.",
     cmd_pairing_read},
    {"pairing", "write", 'I', "<slot> --in FILE --yes-write-pairing-slot", "Write a public pairing key into an empty slot (1-3)",
     "IRREVERSIBLE: a pairing slot is written once. The file holds the 32-byte public key. Slot 0 (the shared engineering key) "
     "is refused. The slot must read empty first; it is read back and compared afterwards, because older firmware can fail "
     "silently. With --dry-run the checks run and nothing is written.",
     cmd_pairing_write},
    {"pairing", "invalidate", 'I', "<slot> --yes-invalidate-pairing-slot [--yes-invalidate-shared-key]", "Invalidate a pairing slot",
     "IRREVERSIBLE. The slot being invalidated must not be the key that opened this session. Slot 0 also needs "
     "--yes-invalidate-shared-key and a session opened with another, working pairing key, so that the chip stays reachable.",
     cmd_pairing_invalidate},
    /* config */
    {"config", "dump", 'R', "[--decode]", "Every R-config and I-config object as raw words",
     "Prints \"R NAME 0xVALUE\" lines for the reconfigurable objects and \"I NAME 0xVALUE\" lines for the one-way "
     "objects. --decode adds, for each access object, which pairing keys may use the command on which group of targets.",
     cmd_config_dump},
    {"config", "get", 'R', "<name>", "One configuration object (the TR01_CFG_ prefix may be left out)",
     "Prints the R-config and the I-config word of the object.", cmd_config_get},
    {"config", "diff", 'R', "--in FILE [--check]", "Compare the chip's R-config with a file",
     "Lists each differing object with which pairing keys would gain or lose access. With --check the exit code is 8 if "
     "anything differs.",
     cmd_config_diff},
    {"config", "restrict-shared", 'R', "--unit-slot N [--out FILE|-]", "Build a config file that takes the shared key's rights away",
     "Reads the chip's current R-config and writes a copy with pairing key 0 (the shared engineering key) removed from every "
     "access object except PING. Refuses unless pairing slot N holds a key and has the right to read and rewrite the "
     "configuration, so the restriction can be undone. Changes nothing on the chip; apply the file with config rewrite.",
     cmd_config_restrict_shared},
    {"config", "rewrite", 'H', "--in FILE --yes-rewrite-r-config [--backup FILE] [--include-non-access]", "Rewrite the R-config from a file",
     "HAZARDOUS. Validates the file, saves the current R-config to a backup, ERASES then writes the R-config, and reads it "
     "back. Writing without erasing can put the chip into permanent Alarm Mode, so this is the only way the tool writes it. "
     "Refuses a file that would take the session key's right to read or rewrite the configuration, and a file that changes "
     "objects that are not access rights unless --include-non-access is given. If the write fails after the erase the "
     "message says so and names the backup.",
     cmd_config_rewrite},
    {"config", "restore", 'H', "--in BACKUP --yes-rewrite-r-config", "Write a saved R-config back",
     "The same safe sequence as rewrite, for a backup file that config rewrite saved.", cmd_config_restore},
    {"config", "burn", 'I', "<name> <bit> --yes-burn-i-config-bit", "Clear one I-config bit (one-way)",
     "IRREVERSIBLE: an I-config bit only goes from 1 to 0. Refused unless the CPU firmware is 2.0.0 or newer (older firmware "
     "can fail the write silently); the bit is read back afterwards.",
     cmd_config_burn},
    /* mnd */
    {"mnd", "setup", 'I', "--state FILE --out KEYFILE --yes-setup-mnd [--slots N] [--base-slot B] [--add-file F] [--replace]", "Set a new PIN (MAC-and-Destroy)",
     "Runs the new-PIN procedure of Tropic Square's application note ODN_TR01_app_002: initialises N chip slots (default 5) "
     "from the base slot, writes the host state (needs no protection) to --state and the released 32-byte key to --out. "
     "The PIN (4 to 8 characters) comes from --pin-file, --pin-stdin or a terminal prompt, never the command line.",
     cmd_mnd_setup},
    {"mnd", "check", 'W', "--state FILE --out KEYFILE [--add-file F]", "Check a PIN and release the key",
     "A wrong PIN destroys one chip slot and uses one attempt; when none remain the key can never be released. The "
     "decremented attempt count is saved before the chip is touched.",
     cmd_mnd_check},
    {"mnd", "status", 'R', "--state FILE", "Attempts left according to the host state",
     "Reads only the state file (no chip access). The chip's own slots are authoritative.", cmd_mnd_status},
    {"mnd", "seal", 'R', "--key-file K --in FILE --out FILE", "Encrypt a file with a released key (AES-256-GCM)",
     "For the PIN-protected pairing key: seals the 32-byte private key under the key mnd check released. Host only.",
     cmd_mnd_seal},
    {"mnd", "unseal", 'R', "--key-file K --in FILE --out FILE", "Decrypt a sealed file",
     "The reverse of seal. Exit code 8 if the key is wrong or the file is damaged. Host only.", cmd_mnd_unseal},
    /* counter */
    {"counter", "get", 'R', "<n>", "Read a monotonic counter (0-15)", "An uninitialised counter is exit code 6.", cmd_counter_get},
    {"counter", "init", 'I', "<n> <value> --yes-init-counter", "Set a monotonic counter (0 to 4294967294)",
     "Per libtropic's functional test a counter can be initialised again, so rollback protection holds only after the "
     "chip's access rights remove the init right from every key. Read back afterwards.",
     cmd_counter_init},
    {"counter", "update", 'I', "<n> --yes-update-counter", "Decrement a monotonic counter by one",
     "Per libtropic's functional test an update decrements by one and fails at zero (refused here before it is tried).",
     cmd_counter_update},
    /* session */
    {"session", "test", 'R', "", "Open and close a secure session; say which key was used",
     "Reports the pairing key and slot that opened the session. Exit code 4 if no key works. With --key pin the PIN-protected "
     "key is used (PIN from --pin-file, --pin-stdin or the terminal).",
     cmd_session_test},
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
