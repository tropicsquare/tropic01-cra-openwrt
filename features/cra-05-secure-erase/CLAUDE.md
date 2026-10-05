# cra-05-secure-erase — rules for AI assistants

- Erasing TROPIC01 key slots or R-MEM is **irreversible**. Never erase
  factory identity slots; confirm with the user before any erase on real
  hardware.
- Follow cra-01-secure-defaults' package layout and shell style.
- The vault key is only ever in R-MEM slot 100 and in RAM while a command runs.
  Never write it to flash, a log or a transcript, and never print it.
- `tropic01-vault init` writes R-MEM slot 100 and `destroy` erases it. Both are
  chip operations on real hardware: confirm with the user for that step, say which
  slot, and never touch slots 0-31 or any other slot.
- The README's "does not protect" list stays accurate: only /vault is protected,
  and with the shared pairing key the board holder can read the key.
