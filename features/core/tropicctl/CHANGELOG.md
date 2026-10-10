# Changelog

## 0.1.0 (unreleased)

Phases 1 to 4 of [SPEC.md](SPEC.md).

- Chip interface (`chip.h`) with two implementations: libtropic 4.2 for the real chip, and a hand-written fake for
  tests.
- Commands: `info chip|fw|mode|cert`, `rng get`, `mem list|read|write|erase`, `ecc list|generate|store|pub|erase|sign`,
  `pairing list|read|write|invalidate`, `config dump|get|diff|restrict-shared|rewrite|restore|burn`,
  `mnd setup|check|status|seal|unseal`, `counter get|init|update`, `session test`, `help`, `version`.
- Safety classes R, W, I, H; confirmation flags named per operation; every write read back; the R-config is only ever
  written by erasing first; the PIN attempt count is saved before the chip is touched.
- `--key pin`: a pairing key protected by a PIN (MAC-and-Destroy), opened with a bootstrap key.
- Global options are accepted anywhere on the command line.
- Global options `--json`, `--dry-run`, `--key`, `--lock-timeout`, device options; stable exit codes 0 to 10; data on
  stdout, notices on stderr; a lock file around the chip.
- Legacy mode when started as `lt-util`: every old command (`-i`, `-r`, `-e -i|-g|-d|-c|-s`, `-m -s|-r|-e`, `-k -r|-w`,
  `-x -r|-f`) with the old messages and exit status.
- Generated reference (`docs/reference.md`) and manual page (`docs/tropicctl.1`), checked against the command table by
  the tests.
