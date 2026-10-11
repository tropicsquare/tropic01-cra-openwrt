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
- `config dump` and `config get` show the R-config alone when the key has no right to read the I-config; `config restore` works with such a key.
- `--key pin`: a pairing key protected by a PIN (MAC-and-Destroy), opened with a bootstrap key.
- `chip reboot`: restart the chip. The chip loads its R-config at start-up, so a rewritten R-config (config rewrite,
  config restore) is not in force until a restart; the fake chip models this.
- `fw status|update|maintenance`: firmware versions, silicon revision and mode; update both mutable firmware banks with
  libtropic's recommended sequence (refuses another silicon revision, a downgrade and a chip with Maintenance Mode
  disabled in the I-config; reads the versions back); turn Maintenance Mode on or off in the R-config.
- `--key prod0` and `--key es01`: the two slot-0 keys libtropic publishes (production and engineering sample); a refused
  shared key suggests prod0.
- Global options are accepted anywhere on the command line.
- Global options `--json`, `--dry-run`, `--key`, `--lock-timeout`, device options; stable exit codes 0 to 10; data on
  stdout, notices on stderr; a lock file around the chip.
- Legacy mode when started as `lt-util`: every old command (`-i`, `-r`, `-e -i|-g|-d|-c|-s`, `-m -s|-r|-e`, `-k -r|-w`,
  `-x -r|-f`) with the old messages and exit status.
- Generated reference (`docs/reference.md`) and manual page (`docs/tropicctl.1`), checked against the command table by
  the tests.
