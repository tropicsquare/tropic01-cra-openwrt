# tropic01: a command-line tool for TROPIC01 on Linux (specification draft)

**Status: draft for review. No code exists yet.** This document defines what the tool does, how it behaves and how it
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
5. Testable without a chip: against libtropic's TROPIC01 model, then on hardware.

**Non-goals**
- Not a PKCS#11 module or an OpenSSL provider (see backlog 3.11); it complements them.
- Not a daemon. A broker that owns the single SPI session is backlog 3.12; until then the tool serialises with a lock
  file (section 6).
- No firmware update in the first version (a later `fw` area, with its own safety review).
- No USB DevKit support; Linux SPI and GPIO only.

## 3. Name, layout, build

- Binary `tropic01`, OpenWrt package `tropic01-tool`. (Name to be confirmed, section 12.)
- C11, libtropic 4.2.x from the feed's single pinned source, OpenSSL crypto layer, Linux SPI hardware layer; the same
  build rules as `lt-util` today (no bundled copies, pinned hash).
- Source layout, so that each file has one job:

```
src/main.c            argument parsing, dispatch
src/cmd_table.c       the command table: name, summary, usage, safety class, handler (the single source of truth)
src/session.c         open the chip, choose the pairing key, start the session; the only place that does
src/keys.c            pairing key sources (shared, per-unit files, PIN-protected)
src/lock.c            the lock file around the chip
src/output.c          text and JSON writers, error reporting
src/errors.c          libtropic status -> exit code and message
src/safety.c          confirmation flags, refusal rules, read-back helpers
src/cmd_info.c  cmd_rng.c  cmd_ecc.c  cmd_mem.c  cmd_pairing.c  cmd_config.c  cmd_mnd.c  cmd_counter.c
docs/                 generated manual page and reference; tests/ ; CHANGELOG.md
```

## 4. Conventions

### 4.1 Invocation

```
tropic01 [global options] <area> <verb> [arguments] [command options]
tropic01 help [area [verb]]      tropic01 version
```

### 4.2 Global options

| Option | Meaning | Default |
|---|---|---|
| `--spi-dev PATH` | SPI device | `/dev/spidev2.0` (build-time default; also `TROPIC01_SPI_DEV`) |
| `--gpio-dev PATH`, `--cs-pin N` | GPIO chip and chip-select line | `/dev/gpiochip0`, 25 |
| `--spi-speed HZ` | SPI clock | 1000000 |
| `--key SOURCE` | which pairing key opens the session (section 5) | `auto` |
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
| `pin` | the PIN-protected key of tier 1 in the cra-04 design: open a bootstrap session, run MAC-and-Destroy with the PIN, decrypt the protected key, open the real session |

`tropic01 session test [--key SOURCE]` opens and closes a session and reports which key and slot were used.

## 6. Serialising access to the chip

The chip allows one secure session at a time. Until a broker exists the tool takes an exclusive `flock` on
`/var/lock/tropic01.lock` for the whole invocation and waits up to `--lock-timeout`; the PKCS#11 module is expected to
take the same lock. This turns "chip busy" from a mystery failure into exit code 3 with a clear message.

## 7. Command reference

Arguments in `<>` are required. Slot numbers are validated before the chip is touched.

### 7.1 `info` (all class R)

| Command | Does |
|---|---|
| `info chip` | chip identification (what `lt-util -i` prints), as text or `--json` |
| `info fw` | RISC-V and SPECT firmware version bytes, and the decoded version where known |
| `info cert [--out FILE]` | the chip's certificate store; verify chain with `--verify` |
| `info mode` | whether the chip is in application, maintenance or start-up mode |

### 7.2 `rng` (R)

`rng get <count> [--out FILE|-]`: 1 to 255 random bytes from the chip; raw to a file or stdout, hex with `--hex`.

### 7.3 `ecc`

| Command | Class | Does |
|---|---|---|
| `ecc list` | R | for each of the 32 slots: empty, or curve and origin (generated or stored) |
| `ecc generate <slot> --curve p256\|ed25519` | W | generate a key in the chip. Refuses an occupied slot unless `--replace` |
| `ecc store <slot> --curve p256\|ed25519 --in FILE` | W | store a private key from a file (libtropic accepts both curves); the key's origin is then "stored", not "generated", which matters for trust |
| `ecc pub <slot> [--out FILE] [--format raw\|pem]` | R | the public key; `pem` writes a standard SubjectPublicKeyInfo |
| `ecc erase <slot>` | W | erase a key |
| `ecc sign <slot> --in FILE [--alg ecdsa\|eddsa] [--digest]` | R | sign. ECDSA signs a 32-byte hash (`--digest` says the input is already a digest, otherwise the tool hashes it with SHA-256); EdDSA signs the message. The algorithm defaults from the slot's curve; a mismatch is an error, not a wrong answer |

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

| Command | Class | Does |
|---|---|---|
| `config dump [--decode]` | R | every R-config and I-config object (`R\|I NAME 0xVALUE`); `--decode` adds the access rights per command, slot group and pairing key |
| `config get <name>` | R | one object |
| `config diff <file>` | R | compare the chip's R-config with a file |
| `config rewrite --in FILE --yes-rewrite-r-config` | **H** | validate the file, save the current R-config next to it, **erase, write, read back**, compare. If the write fails after the erase the tool says so and prints the backup path. Refuses a file that would lock the session's own pairing key out of reconfiguring |
| `config restore --in BACKUP --yes-rewrite-r-config` | **H** | same sequence from a backup |
| `config burn <name> <bit> --yes-burn-i-config-bit` | **I** | clear one I-config bit (one-way); allowed only on firmware that reports write errors (2.0.0 or newer); read back |

There is no raw "write one R-config object" command, because the chip's erratum makes it dangerous.

### 7.7 `mnd`: MAC-and-Destroy (PIN verification)

Implements the vendor's PIN-verification scheme on libtropic's `lt_mac_and_destroy()`, not hand-written code.

| Command | Class | Does |
|---|---|---|
| `mnd setup --state FILE [--pin-file F] [--add HEX] [--slots N] [--secret-file F]` | **I** | the new-PIN procedure; writes the host-side state (ciphertexts, tag, counter) to FILE; the chip's slots are consumed |
| `mnd check --state FILE [--pin-file F] [--add HEX] --out FILE` | W | the PIN entry check; on success writes the released 32-byte key, on failure a destroyed slot has been used up |
| `mnd status --state FILE` | R | remaining attempts according to the host state |

PIN length 4 to 8 characters, additional data up to 128 bytes, as in the vendor note. Wiping-PIN and multi-PIN
extensions are later work.

### 7.8 `counter` (monotonic counters, for rollback protection)

`counter get <n>` (R), `counter init <n> <value> --yes-init-counter` (**I**; the value must be in 0 to the library's
`TR01_MCOUNTER_VALUE_MAX`), `counter update <n> --yes-update-counter` (**I**). libtropic's header does not say which
direction an update moves the counter or what happens at the limit; **both must be confirmed from the datasheet
before this command is written**, and the tool must refuse an update it knows would pass the limit.

### 7.9 `session test`

See section 5. Class R.

## 8. Output formats

Text is one fact per line. JSON is one object on stdout; errors are an object on stderr:

```
$ tropic01 --json pairing list
{"pairing_slots":[{"slot":0,"state":"populated"},{"slot":1,"state":"empty"},
                  {"slot":2,"state":"empty"},{"slot":3,"state":"empty"}],"key":{"source":"shared","slot":0}}

$ tropic01 --json mem read 1 --out /tmp/c1
{"slot":1,"bytes":299,"file":"/tmp/c1"}

$ tropic01 --json pairing write 0 --in k.pub --yes-write-pairing-slot ; echo $?
{"error":{"code":9,"name":"refused","message":"pairing slot 0 holds the shared engineering key and is never written"}}
9
```

JSON field names are part of the interface and changes are listed in `CHANGELOG.md`.

## 9. Documentation plan

- `tropic01 help …` prints from the command table; `make man` produces the manual page from the same table.
- `docs/guide.md`: first steps, key handling, a worked provisioning example, recovery notes (what a lost PIN means).
- `docs/reference.md`: generated: every command, option, class, exit code.
- `docs/safety.md`: the safety classes, the chip hazards (R-config erase-before-write, one-way I-config bits, slots
  written once) and why each rule exists.
- A test fails if a command is missing from the table or from the generated reference.

## 10. Test plan

| Level | What | Chip |
|---|---|---|
| Unit | argument parsing, exit-code mapping, JSON output, the safety gate, the config validator and decoder | none |
| Model | every command against libtropic's TROPIC01 software model: sessions, access rights, slot lifecycles, config erase and write, MAC-and-Destroy | model |
| Sandbox | callers (vault, provisioning script) against a fake `tropic01` | none |
| Hardware | the bench suite: today's 13 chip checks, the vault, a chip-signed CSR, plus the new read-only commands; irreversible commands only on a spare chip | bench / spare |

A rule for every **I** and **H** command: a test that it refuses without its flag, and a test that breaking the guard
makes a test fail (as done for `tropic01-pairing`). The model's fidelity to the engineering-sample silicon is checked
by running the read-only commands on the bench chip and comparing.

## 11. Migration from `lt-util`

A compatibility script `lt-util` (installed by the same package) maps the old flags, prints a deprecation notice to
stderr, and returns 1 for any non-zero exit as before. It exists so callers can move one at a time.

| Old | New |
|---|---|
| `lt-util -i` | `tropic01 info chip` |
| `lt-util -r <n> <file>` | `tropic01 rng get <n> --out <file>` |
| `lt-util -e -g <slot>` | `tropic01 ecc generate <slot> --curve p256` |
| `lt-util -e -d <slot> <file>` | `tropic01 ecc pub <slot> --out <file>` |
| `lt-util -e -c <slot>` | `tropic01 ecc erase <slot>` |
| `lt-util -e -i <slot> <file>` | `tropic01 ecc store <slot> --curve ed25519 --in <file>` (the old command only stores Ed25519) |
| `lt-util -e -s <slot> <in> <out>` | `tropic01 ecc sign <slot> --in <in> --out <out>` (now also correct for P-256 slots) |
| `lt-util -m -s <slot> <file>` | `tropic01 mem write <slot> --in <file>` |
| `lt-util -m -r <slot> <file>` | `tropic01 mem read <slot> --out <file>` |
| `lt-util -m -e <slot>` | `tropic01 mem erase <slot>` |
| `lt-util -k -r <slot> <file>` | `tropic01 pairing read <slot> --out <file>` |
| `lt-util -k -w <slot> <file> --irreversible` | `tropic01 pairing write <slot> --in <file> --yes-write-pairing-slot` |
| `lt-util -x -r` | `tropic01 config dump` |
| `lt-util -x -f` | `tropic01 info fw` |
| `LT_UTIL_FORCE_SHARED_KEY=1` | `--key shared` (the variable stays honoured by the compatibility script) |

Callers to move, and what each depends on today:

| Caller | Uses | Note |
|---|---|---|
| `tropic01-vault` | `-i`, `-r`, `-m -s/-r/-e` | exit status only; slot 100 only |
| `tests/hardware/bench.py` (`cra04`) | `-i`, `-m -r`, `-k -r/-w`, message texts such as "Error sec channel" and "per-unit pairing key, slot" | must switch to exit codes and `--json` |
| `tropic01-pairing` (draft) | `-i`, `-k -r/-w`, `-m -r`, `-e -d`, `-x -r/-w` | becomes a thin wrapper around `pairing`, `config rewrite` and `config restore` |
| `tropic01-aws-provision` | `lt-util --generate-key --slot 1`, `--write-data --slot 2 --file …` | **these long options never existed**; rewrite against the new interface (backlog 3.1) |
| user guides and skills (about 20 documents) | `-e -g`, `-e -d`, `-m -s`, `-i` | search and replace, checked by the link and example checks |

## 12. Decisions for the reviewer

1. **Name:** `tropic01` (binary) and `tropic01-tool` (package), or keep `lt-util` and break the CLI at 1.0?
2. **Home:** start inside this feed (`features/core/tropic01-tool/`) and offer it to Tropic Square's `libtropic-util`
   once stable, or start upstream?
3. **Scope of version 1:** `info`, `rng`, `ecc`, `mem`, `pairing`, `config`, `session` first; `mnd` and `counter`
   second. Agree?
4. **JSON:** worth the code now, or text only first?
5. **Lock file:** accept `/var/lock/tropic01.lock` as the stop-gap, with the PKCS#11 module patched to take it too?
6. **Model testing:** accept libtropic's software model as the main automated test (needs a Python-based model and
   its dependencies in CI), or keep to a hand-written fake plus hardware?
7. **Raw R-config writes:** keep the rule that none exists, even for experts?

## 13. Phases and acceptance

| Phase | Deliverable | Done when |
|---|---|---|
| 0 | this specification reviewed and the decisions above made | decisions recorded here |
| 1 | skeleton: option parsing, command table, session, lock, output, exit codes, `info`, `rng`, `mem`, `pairing list/read`, `config dump`; generated help and man page; unit tests | builds in the feed; bench read-only checks pass against the real chip |
| 2 | `ecc`, `pairing write/invalidate`, `config rewrite/restore/diff/burn`; model tests; guard-breaking tests | model suite passes; spare-chip run documented |
| 3 | compatibility script; callers migrated; `lt-util` patches retired | the full hardware suite (56 checks) and an OpenVPN tunnel pass on the new tool |
| 4 | `mnd`, `counter`; PIN-protected sessions for cra-04 tier 1 | spare-chip run documented |
