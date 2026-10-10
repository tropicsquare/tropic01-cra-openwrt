# Safety

Every command declares one safety class, shown in `tropicctl help` and in the [reference](reference.md).

| Class | Meaning | Gate |
|---|---|---|
| **R** read-only | changes nothing on the chip | none |
| **W** reversible write | can be undone by another command (an R-MEM slot, an ECC key) | `--dry-run` shows what would happen |
| **I** irreversible | cannot be undone: a pairing-slot write or invalidation, an I-config bit, counter initialisation | a named `--yes-<operation>` flag; the target is re-read first and must be in the expected state |
| **H** hazardous | can leave the chip unusable if done wrong: rewriting the R-config | the same flag, and the tool performs the safe sequence itself |

Phase 1 contains only R and W commands. The rules below apply to the later ones.

## Chip hazards the tool is built around

- **Pairing slots are written once** (they live in I-Memory) and can be invalidated, also irreversibly. Slot 0 holds
  the shared engineering-sample key and is never written by the tool.
- **R-config must be erased before it is written.** libtropic documents that writing it without erasing first can
  put the chip into permanent Alarm Mode (erratum OI_TR01_ERR_2026010800). `tropicctl` will have no raw
  "write one R-config object" command; a rewrite will erase, write and read back by itself.
- **I-config bits only go from 1 to 0** and cannot be set again.
- **I-Memory writes** on firmware older than 2.0.0 can fail silently outside -20 to 85 C; every write is read back.
- **One secure session at a time.** The tool serialises with a lock file; a second user gets exit code 3.

## Secrets

PINs and private keys are never taken on the command line (they would show in `ps` and shell history). Private key
material is never printed or logged; files the tool creates are mode 0600.

## Checks in the code

Each safety rule has a test, and the tests are checked by breaking the guard: removing a guard must make a test
fail. The fake chip's rules are labelled measured or assumed (see the header of `src/chip_fake.c`).
