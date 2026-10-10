# tropicctl: a command-line tool for TROPIC01 on Linux (specification draft)

**Status: all decisions made (section 12); phases 1 to 4 implemented. Phase 1 was run read-only on the bench chip; everything that writes the chip is tested against the fake chip only, until a spare chip is available.** This document defines what the tool does, how it behaves and how it
is tested, so that the interface can be agreed before it is built. It replaces the patched `lt-util` that this feed
carries today and is written so that it could also be used outside this project.

## 1. Why a new tool

`lt-util` is a single `main.c` of about 1,300 lines with six patches on top. In this project that cost us:

- the same open-chip and start-session code in every command, which produced two crashes in the libtropic 4.2 port
  (an uninitialised handle and a missing crypto context);
- exit codes that are only 0 or 1, errors that appear only because of a compile-time log level, and data and notices
  mixed on one stream, so scripts guess failures from message text;
- no model of what is irreversible: a pairing-slot write and a read look alike, and the guard is a literal flag a
  single command happens to check;
- documentation that drifted from the real command line (the AWS provisioning script still calls long options that
  never existed);
- no way to add configuration writes, PIN-based sessions or machine-readable output without another patch.

## 2. Goals and non-goals

**Goals**
1. Every capability libtropic 4.2 offers on a Linux SPI board that is useful to an operator or a provisioning script,
   behind one consistent interface.
2. Safe by construction: every command has a declared safety class, irreversible and hazardous operations need an
   explicit confirmation flag, writes are read back, and the chip-specific hazards (erase R-config before writing it)
   are enforced inside the tool.
3. Scriptable: stable exit codes, `--json`, data on stdout and notices on stderr.
4. Documented from one source: `--help`, the manual page and the reference below are generated from the same command
   table, so they cannot disagree with the code.
5. Testable without a chip: through a hand-written fake chip behind the chip interface, then on hardware.

**Non-goals**
- Not a PKCS#11 module or an OpenSSL provider (see backlog 3.11); it complements them.
- Not a daemon. A broker that owns the single SPI session is backlog 3.12; until then the tool serialises with a lock
  file (section 6).
- No firmware update in the first version (a later `fw` area, with its own safety review).
- No USB DevKit support; Linux SPI and GPIO only.

## 3. Name, packaging, layout, build

- Binary **`tropicctl`**, OpenWrt package `tropicctl`. Decided 2026-10-10.
- **A package in this feed, written to be movable.** The tool lives in one self-contained directory,
  `features/core/tropicctl/`, with its own `CMakeLists.txt`, `src/`, `tests/`, `docs/`, `README.md`, `CHANGELOG.md` and
  `LICENSE`, and nothing in it refers to files elsewhere in this repository. The OpenWrt `Makefile` is the only part
  that knows about the feed; it builds from the local `src/`. Moving the tool to its own repository later means
  copying the directory and changing that one Makefile to fetch a pinned release (`PKG_SOURCE_URL` and
  `PKG_MIRROR_HASH`); the tests and documentation go with it. The feed's `make test` and CI call the tool's own test
  entry point, so they do not need to change either.
- C11, libtropic 4.2.x from the feed's single pinned source (`-DLIBTROPIC_SRC`), OpenSSL crypto layer, Linux SPI
  hardware layer; no bundled copies. For development outside OpenWrt it builds on a normal Linux host with the same
  CMake file.
- **The chip sits behind an interface.** Commands never call libtropic directly; they use `chip.h` (open, session,
  ecc, mem, pairing, config, mnd, counter, info). There are two implementations selected at build time: the real one
  on libtropic, and a hand-written fake that keeps its state in files and models slots, access rights, the R-config
  erase-and-write rule and the failure cases seen on real chips. This is how the command logic and every safety rule
  are tested on a host without a chip (section 10).
- Source layout, so that each file has one job:

```
CMakeLists.txt  Makefile (OpenWrt)  README.md  CHANGELOG.md  LICENSE
src/main.c            argument parsing, dispatch, legacy mode (section 11)
src/cmd_table.c       the command table: name, summary, usage, safety class, handler (the single source of truth)
src/chip.h            the chip interface
src/chip_libtropic.c  the real implementation (the only file that includes libtropic.h)
src/chip_fake.c       the hand-written fake, for tests only
src/session.c         choose the pairing key, start the session; the only place that does
src/keys.c            pairing key sources (shared, per-unit files, PIN-protected)
src/lock.c            the lock file around the chip
src/output.c          text and JSON writers, error reporting
src/errors.c          libtropic status -> exit code and message
src/safety.c          confirmation flags, refusal rules, read-back helpers
src/crypto.c          host-side cryptography (OpenSSL): keyed hash, AES-GCM, randomness, encoders
src/uap.c             the access-word layout: kinds of objects, who may do what, decoding
src/cfgfile.c         configuration files (the raw words `config dump` prints)
src/mnd.c             the PIN scheme of the application note
src/pin.c             reading a PIN without the command line
src/ecc_host.c        ECC with OpenSSL, used only by the fake chip
src/cmd_info.c  cmd_rng.c  cmd_ecc.c  cmd_mem.c  cmd_pairing.c  cmd_config.c  cmd_mnd.c  cmd_counter.c  cmd_session.c  cmd_help.c
docs/                 generated manual page and reference
tests/                unit tests, fake-chip command tests, hardware test scripts
```

### 3.1 Who needs what

Scope of version 1 is what this project's CRA features need, plus every command `lt-util` has today.

| Feature | Needs |
|---|---|
| cra-02 verified boot (rollback protection) | `counter`, `info fw`, `ecc sign` / `ecc pub` (signature checks) |
| cra-03 key storage, VPN and cloud identity | `ecc generate`, `ecc pub`, `ecc sign`, `mem write`/`read` (certificates), `info chip` |
| cra-04 access control | `pairing`, `config`, `mnd`, `session test`, `--key pin` |
| cra-05 vault and storage encryption | `rng get`, `mem write`/`read`/`erase`, `mnd` (PIN-unlocked storage) |
| cra-06 security log (future anchoring) | `ecc sign`, `mem write`, `counter` |
| everything today | the `lt-util` commands (section 11) |

## 4. Conventions

### 4.1 Invocation

```
tropicctl [global options] <area> <verb> [arguments] [command options]
tropicctl help [area [verb]]      tropicctl version
```

### 4.2 Global options

Global options are accepted anywhere on the command line, before or after the command.

| Option | Meaning | Default |
|---|---|---|
| `--spi-dev PATH` | SPI device | `/dev/spidev2.0` (build-time default; also `TROPIC01_SPI_DEV`) |
| `--gpio-dev PATH`, `--cs-pin N` | GPIO chip and chip-select line | `/dev/gpiochip0`, 25 |
| `--spi-speed HZ` | SPI clock | 1000000 |
| `--key SOURCE` | which pairing key opens the session (section 5) | `auto` |
| `--pin-file FILE`, `--pin-stdin` | where a PIN comes from (never the command line); otherwise a terminal prompt | |
| `--json` | machine-readable output on stdout | off |
| `-q`, `-v` | quiet / verbose notices on stderr | normal |
| `--lock-timeout SEC` | how long to wait for the chip lock | 10 |
| `--dry-run` | check everything read-only, say what would be written, write nothing | off |

### 4.3 Streams and exit codes

Data and results go to **stdout**; notices, warnings and errors go to **stderr**. Exit codes:

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | internal error (a bug) |
| 2 | usage error (bad arguments) |
| 3 | chip unreachable or busy (SPI, GPIO, lock timeout) |
| 4 | secure session failed (no usable pairing key, key refused) |
| 5 | access denied by the chip's access rights |
| 6 | slot empty or object not found |
| 7 | slot already written (a one-time slot) |
| 8 | verification failed (read-back mismatch, bad signature) |
| 9 | refused by a safety rule (missing confirmation flag, forbidden target) |
| 10 | the chip reported another error (its status is in the message) |

### 4.4 Safety classes

Every command declares one class, shown in `help` and in the manual page. The gate is enforced by `safety.c`, not by
each command.

| Class | Meaning | Gate |
|---|---|---|
| **R** read-only | changes nothing on the chip | none |
| **W** reversible write | can be undone by another command (an R-MEM slot, an ECC key) | none, but `--dry-run` is available |
| **I** irreversible | cannot be undone: pairing-slot write or invalidation, I-config bit, counter initialisation | `--yes-<operation>` (named per operation, section 7); the target must be re-read first and found in the expected state |
| **H** hazardous | can leave the chip unusable if done wrong: rewriting R-config | the same flag, plus the tool performs the safe sequence itself (erase then write then read back); never exposed as a raw write |

### 4.5 Secrets

PINs and private keys are never accepted on the command line (they would show in `ps` and shell history). PINs come
from `--pin-file PATH`, `--pin-stdin` or a terminal prompt. Private key material is never printed or logged; files the
tool creates are `0600`.

## 5. Sessions and pairing keys

One invocation opens one secure session and closes it. The key source is `--key`:

| Source | Meaning |
|---|---|
| `auto` (default) | per-unit key files if they exist (`/etc/tropic01/pairing.key`, `.pub`, `.slot`), else the shared engineering-sample key with a warning on stderr. **Files that exist but are unusable are an error, never a silent fallback** (kept from today) |
| `shared` | the shared engineering-sample key (slot 0); needed to write the per-unit key |
| `unit` | the per-unit key files only; error if absent |
| `file:PATH:SLOT` | an explicit key file and slot |
| `pin` | the PIN-protected key of tier 1 in the cra-04 design: open a bootstrap session with the per-unit key, run MAC-and-Destroy with the PIN (`pin.state`, optional `pin.add`), decrypt the protected private key (`protected.enc`, sealed with `mnd seal`) for the pairing slot in `protected.slot`, end the bootstrap session and open the real one. A wrong PIN uses an attempt; with none left the key can never be opened again |

`tropicctl session test [--key SOURCE]` opens and closes a session and reports which key and slot were used.

## 6. Serialising access to the chip

The chip allows one secure session at a time. Until a broker exists the tool takes an exclusive `flock` on
`/var/lock/tropicctl.lock` for the whole invocation and waits up to `--lock-timeout`; the PKCS#11 module is expected to
take the same lock. This turns "chip busy" from a mystery failure into exit code 3 with a clear message.

## 7. Command reference

Arguments in `<>` are required. Slot numbers are validated before the chip is touched.

### 7.1 `info` (all class R)

| Command | Does |
|---|---|
| `info chip` | chip identification (what `lt-util -i` prints), as text or `--json` |
| `info fw` | RISC-V and SPECT firmware version bytes as the chip reports them |
| `info cert [--out-dir DIR]` | the chip's certificate store: the length of each certificate, and with `--out-dir` each as `certN.der`; verifying the chain is not done |
| `info mode` | whether the chip is in application, maintenance or start-up mode |

### 7.2 `rng` (R)

`rng get <count> [--out FILE|-]`: 1 to 255 random bytes from the chip; raw to a file or stdout, hex with `--hex`.

### 7.3 `ecc`

| Command | Class | Does |
|---|---|---|
| `ecc list` | R | for each of the 32 slots: empty, or curve and origin (generated or stored) |
| `ecc generate <slot> --curve p256\|ed25519 [--replace --yes-erase-ecc-key]` | W | generate a key inside the chip; an occupied slot is refused, `--replace` erases it first and needs the flag because the old key is lost for good; read back and checked |
| `ecc store <slot> --curve p256\|ed25519 --in FILE [--replace --yes-erase-ecc-key]` | W | store a 32-byte private key from a file; its origin is then "stored", not "generated", which matters for trust |
| `ecc pub <slot> [--out FILE\|-] [--format raw\|pem]` | R | the public key (raw 64 bytes X\|\|Y for P-256, 32 bytes for Ed25519, or a SubjectPublicKeyInfo PEM) |
| `ecc erase <slot> --yes-erase-ecc-key` | **I** | destroys the key for good (a generated key cannot be recreated), checks the slot reads empty |
| `ecc sign <slot> --in FILE [--out FILE\|-] [--alg ecdsa\|eddsa] [--digest] [--der]` | R | ECDSA signs a 32-byte hash (the input is hashed with SHA-256 unless `--digest` says it already is a digest); EdDSA signs the message (up to 4095 bytes). The algorithm follows the slot's curve; a mismatch is refused, not answered wrongly. Output is 64 raw bytes, or DER with `--der` |

### 7.4 `mem` (R-MEM data slots 0 to 511; 444 bytes, 475 on libtropic 4.2)

| Command | Class | Does |
|---|---|---|
| `mem list [--range A-B]` | R | which slots hold data and how long it is |
| `mem read <slot> [--out FILE\|-]` | R | read a slot |
| `mem write <slot> --in FILE` | W | write; refuses an occupied slot unless `--replace`; reads back and compares |
| `mem erase <slot>` | W | erase, then check the slot reads empty |

### 7.5 `pairing`

| Command | Class | Does |
|---|---|---|
| `pairing list` | R | for slots 0 to 3: empty, populated, or invalidated |
| `pairing read <slot> [--out FILE]` | R | the 32-byte public key of a slot |
| `pairing write <slot> --in FILE --yes-write-pairing-slot` | **I** | slot 1 to 3 only, must read empty first, written once, read back and compared. Slot 0 is refused (exit 9) |
| `pairing invalidate <slot> --yes-invalidate-pairing-slot` | **I** | refused for slot 0 unless `--yes-invalidate-shared-key` is also given **and** another populated slot has opened a session in this invocation |

### 7.6 `config`

The access objects are described by libtropic's headers and its hardware-wallet example: four bytes per object, one byte
per group of targets (ECC slots in groups of 8, counters in groups of 4, MAC-and-Destroy slots in groups of 32, one
byte per pairing slot for the pairing-key commands, byte 0 only for single-target commands), the four low bits of a
byte standing for pairing keys 0 to 3, a set bit granting access. The file format is the raw words `config dump`
prints, so nothing is invented between the file and the chip.

| Command | Class | Does |
|---|---|---|
| `config dump [--decode]` | R | every R-config and I-config object (`R\|I NAME 0xVALUE`); `--decode` adds which keys may use each command on which target group |
| `config get <name>` | R | one object (the `TR01_CFG_` prefix may be left out) |
| `config diff --in FILE [--check]` | R | compare the chip's R-config with a file, naming which pairing keys gain or lose access; `--check` exits 8 if anything differs |
| `config restrict-shared --unit-slot N [--out FILE\|-]` | R | build a file from the chip's current R-config with pairing key 0 (the shared key) removed from every access object except PING. Refuses unless slot N holds a key and has the right to read and rewrite the configuration. Changes nothing on the chip |
| `config rewrite --in FILE --yes-rewrite-r-config [--backup FILE] [--include-non-access]` | **H** | validate, save the current R-config to a backup, **erase, write, read back**. Refuses a file that would take the right to read or rewrite the configuration from the key of this session, a file that changes objects that are not access rights (unless `--include-non-access`), and any case where the backup cannot be written. If the write fails after the erase it says so and names the backup |
| `config restore --in BACKUP --yes-rewrite-r-config` | **H** | the same sequence for a backup file |
| `config burn <name> <bit> --yes-burn-i-config-bit` | **I** | clear one I-config bit (one-way); refused unless the firmware version bytes show CPU firmware 2.0.0 or newer; read back |

There is no raw "write one R-config object" command, because the chip's erratum makes it dangerous: only
`chip_rconfig_rewrite()` writes the R-config, and it always erases first.

### 7.7 `mnd`: MAC-and-Destroy (PIN verification)

Implements the vendor's PIN-verification scheme (application note ODN_TR01_app_002) on the chip's MAC-and-Destroy
sequence. The PIN (4 to 8 characters) comes from `--pin-file`, `--pin-stdin` or a terminal prompt; additional data from a
file, because it is secret too.

| Command | Class | Does |
|---|---|---|
| `mnd setup --state FILE --out KEYFILE --yes-setup-mnd [--slots N] [--base-slot B] [--add-file F] [--replace]` | **I** | the new-PIN procedure: initialises N chip slots (default 5) from slot B, writes the host state (ciphertexts, tag, attempts left; needs no protection) and the released 32-byte key. Refuses to overwrite an existing state |
| `mnd check --state FILE --out KEYFILE [--add-file F]` | W | the PIN entry check. **The decremented attempt count is saved before the chip is touched.** A wrong PIN is exit 8 and uses an attempt; with none left the key can never be released (exit 9) |
| `mnd status --state FILE` | R | attempts left according to the host state (no chip access) |
| `mnd seal --key-file K --in FILE --out FILE` | R | AES-256-GCM encrypt a file (for the protected pairing key) under a released key; host only |
| `mnd unseal --key-file K --in FILE --out FILE` | R | the reverse; exit 8 if the key is wrong or the file damaged |

The master-secret input and the multiple-PIN and wiping-PIN extensions of the note are not implemented.

### 7.8 `counter` (monotonic counters, for rollback protection)

Confirmed from libtropic's functional test (`lt_test_rev_mcounter`): a counter holds 0 to 0xFFFFFFFE; `init` may be
called again at any time; `update` decrements by one; an update at zero fails. The datasheet defines no default value, so
an uninitialised counter is reported as not found.

| Command | Class | Does |
|---|---|---|
| `counter get <n>` | R | read a counter (0 to 15) |
| `counter init <n> <value> --yes-init-counter` | **I** | set a counter, read back |
| `counter update <n> --yes-update-counter` | **I** | decrement by one, read back; refused when it is already 0 |

Both write commands are classed I even though `init` can be repeated: the point of a rollback counter is that nobody can
raise it again, and that holds only once the chip's access rights remove the `MCOUNTER_INIT` right from every pairing key.
That is a configuration step (`config rewrite`), not something this command does.

### 7.9 `session test`

See section 5. Class R.

## 8. Output formats

Text is one fact per line. JSON is one object on stdout; errors are an object on stderr:

```
$ tropicctl --json pairing list
{"pairing_slots":[{"slot":0,"state":"populated"},{"slot":1,"state":"empty"},
                  {"slot":2,"state":"empty"},{"slot":3,"state":"empty"}],"key":{"source":"shared","slot":0}}

$ tropicctl --json mem read 1 --out /tmp/c1
{"slot":1,"bytes":299,"file":"/tmp/c1"}

$ tropicctl --json pairing write 0 --in k.pub --yes-write-pairing-slot ; echo $?
{"error":{"code":9,"name":"refused","message":"pairing slot 0 holds the shared engineering key and is never written"}}
9
```

JSON field names are part of the interface and changes are listed in `CHANGELOG.md`.

## 9. Documentation plan

- `tropicctl help …` prints from the command table; `make man` produces the manual page from the same table.
- `docs/guide.md`: first steps, key handling, a worked provisioning example, recovery notes (what a lost PIN means).
- `docs/reference.md`: generated: every command, option, class, exit code.
- `docs/safety.md`: the safety classes, the chip hazards (R-config erase-before-write, one-way I-config bits, slots
  written once) and why each rule exists.
- A test fails if a command is missing from the table or from the generated reference.

## 10. Test plan

Hand-written fake plus hardware. There is no dependency on libtropic's software model.

| Level | What | Chip |
|---|---|---|
| Unit | argument parsing, exit-code mapping, JSON output, the safety gate, the config validator and decoder | none |
| Fake chip | every command through `chip_fake.c`, on a host and in CI: slot lifecycles (a pairing slot is written once, an occupied slot is refused), access rights per key and slot group, R-config erase-then-write, failures (silent write failure, corrupted write, chip down), MAC-and-Destroy attempts | fake |
| Sandbox | callers (vault, provisioning script) against a fake `tropicctl` | none |
| Hardware | the bench suite: today's 13 chip checks, the vault, a chip-signed CSR, plus the new read-only commands; irreversible commands only on a spare chip | bench / spare |

Rules for the fake, so it cannot give false comfort:
- Every rule it models is labelled **measured** (checked on a chip, with the date and report) or **assumed**
  (taken from headers or the datasheet). A test that depends on an assumed rule says so.
- Each hardware run re-checks the measured rules, and any difference is fixed in the fake first.
- The read-only commands are compared between the fake's defaults and the bench chip (the survey in the hardware
  report is the first such comparison).

Rules for every **I** and **H** command: a test that it refuses without its flag, and a mutation check showing that
breaking the guard makes a test fail (as done for the `tropic01-pairing` draft).

## 11. Migration from `lt-util` (inheriting its commands)

Every `lt-util` command is kept. The package installs a symlink `lt-util` to `tropicctl`; when the program is started
under the name `lt-util` it runs in **legacy mode**: it accepts the old flags, behaves as before (same text output for
`-i`, exit status 0 or 1, the same notice and error texts that callers parse today, for example "using the per-unit
pairing key, slot N" and "Error sec channel"), and prints a one-line deprecation notice on stderr that
`TROPICCTL_NO_DEPRECATION=1` silences. Callers can then move one at a time. The new commands are not available in
legacy mode, except the two survey commands that already exist (`-x -r`, `-x -f`).

| Old | New |
|---|---|
| `lt-util -i` | `tropicctl info chip` |
| `lt-util -r <n> <file>` | `tropicctl rng get <n> --out <file>` |
| `lt-util -e -g <slot>` | `tropicctl ecc generate <slot> --curve p256` |
| `lt-util -e -d <slot> <file>` | `tropicctl ecc pub <slot> --out <file>` |
| `lt-util -e -c <slot>` | `tropicctl ecc erase <slot> --yes-erase-ecc-key` (legacy mode supplies the flag: the old tool had no gate) |
| `lt-util -e -i <slot> <file>` | `tropicctl ecc store <slot> --curve ed25519 --in <file>` (the old command only stores Ed25519) |
| `lt-util -e -s <slot> <in> <out>` | `tropicctl ecc sign <slot> --in <in> --out <out>` (now also correct for P-256 slots) |
| `lt-util -m -s <slot> <file>` | `tropicctl mem write <slot> --in <file>` |
| `lt-util -m -r <slot> <file>` | `tropicctl mem read <slot> --out <file>` |
| `lt-util -m -e <slot>` | `tropicctl mem erase <slot>` |
| `lt-util -k -r <slot> <file>` | `tropicctl pairing read <slot> --out <file>` |
| `lt-util -k -w <slot> <file> --irreversible` | `tropicctl pairing write <slot> --in <file> --yes-write-pairing-slot` |
| `lt-util -x -r` | `tropicctl config dump` |
| `lt-util -x -f` | `tropicctl info fw` |
| `LT_UTIL_FORCE_SHARED_KEY=1` | `--key shared` (the variable stays honoured in legacy mode) |

Callers to move, and what each depends on today:

| Caller | Uses | Note |
|---|---|---|
| `tropic01-vault` | `-i`, `-r`, `-m -s/-r/-e` | exit status only; slot 100 only |
| `tests/hardware/bench.py` (`cra04`) | `-i`, `-m -r`, `-k -r/-w`, message texts such as "Error sec channel" and "per-unit pairing key, slot" | must switch to exit codes and `--json` |
| `tropic01-pairing` (draft) | `-i`, `-k -r/-w`, `-m -r`, `-e -d`, `-x -r/-w` | becomes a thin wrapper around `tropicctl pairing`, `config rewrite` and `config restore` |
| `tropic01-aws-provision` | `lt-util --generate-key --slot 1`, `--write-data --slot 2 --file …` | **these long options never existed**; rewrite against the new interface (backlog 3.1) |
| user guides and skills (about 20 documents) | `-e -g`, `-e -d`, `-m -s`, `-i` | search and replace, checked by the link and example checks |

## 12. Decisions

Decided by the project owner on 2026-10-10:
1. **Name:** `tropicctl`.
2. **Home:** an OpenWrt package in this feed, structured so it can move to a separate repository later (section 3).
3. **Scope of version 1:** everything this project's CRA features need, and the existing `lt-util` commands
   inherited through legacy mode (sections 3.1 and 11).
4. **Testing:** a hand-written fake chip plus hardware tests only (section 10).

Confirmed by the project owner on 2026-10-10 (proposals accepted):
5. **JSON output** is in version 1.
6. **Lock file** `/var/lock/tropicctl.lock` is the stop-gap around the chip; the PKCS#11 module is patched to take it
   too. `--no-lock` exists for tests and debugging only.
7. **No raw R-config write command**, even for experts.

## 13. Phases and acceptance

Version 1.0 contains every area. Status on 2026-10-11:

| Phase | Deliverable | Status |
|---|---|---|
| 0 | specification reviewed; decisions 1 to 7 made | **done** |
| 1 | skeleton, chip interface and fake, `info`, `rng`, `mem`, `pairing list/read`, `config dump`, `session test`, legacy mode for the read-only and R-MEM commands, generated help and manual page | **done**; run read-only on the bench chip |
| 2 | `ecc`, `pairing write/invalidate`, `config get/diff/restrict-shared/rewrite/restore/burn`, the rest of legacy mode, guard-breaking tests | **done against the fake chip** (signatures verified by OpenSSL; every guard checked by mutation); **not run on a spare chip**; the real-chip build compiles |
| 3 | callers migrated (vault, AWS provisioning script, bench, docs); the `lt-util` package and its patches retired; `lt-util` kept as a symlink | **done in the tree**; the bench's read-only `cra04` run and the vault run on the real chip pass on the new binary; the full hardware suite on a flashed image is still to run |
| 4 | `mnd`, `counter`, PIN-protected sessions (`--key pin`) | **done against the fake chip**; not run on a spare chip |

What is still unconfirmed on a real chip, because it needs a write that is not safe on the bench unit: the access-word layout
as the chip applies it (it is libtropic's own description), the effect of erasing the R-config, the MAC-and-Destroy sequence's
behaviour with the host scheme, and the counter commands. Each fake rule that depends on one of these is marked `[assumed]`
in `src/chip_fake.c`.
