# Changelog

## 0.1.0 (unreleased)

Phase 1 of [SPEC.md](SPEC.md).

- Chip interface (`chip.h`) with two implementations: libtropic 4.2 for the real chip, and a hand-written fake for
  tests.
- Commands: `info chip`, `info fw`, `rng get`, `mem list|read|write|erase`, `pairing list|read`, `config dump`,
  `session test`, `help`, `version`.
- Global options `--json`, `--dry-run`, `--key`, `--lock-timeout`, device options; stable exit codes 0 to 10; data on
  stdout, notices on stderr; a lock file around the chip.
- Legacy mode when started as `lt-util`: `-i`, `-r`, `-m -s|-r|-e`, `-k -r`, `-x -r`, `-x -f`.
- Generated reference (`docs/reference.md`) and manual page (`docs/tropicctl.1`), checked against the command table by
  the tests.
