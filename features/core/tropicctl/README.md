# tropicctl

A command-line tool for the [TROPIC01](https://tropicsquare.com/tropic01) secure element on Linux (SPI and GPIO). It
reads chip and firmware information, gets random numbers, stores data in R-MEM, reads pairing keys and the chip
configuration, and is built so that scripts can rely on it: stable exit codes, optional JSON, data on stdout and
notices on stderr, and a safety class on every command.

**Status: phase 1 of [SPEC.md](SPEC.md).** Read-only commands and R-MEM are implemented. ECC keys, pairing-key writes,
configuration rewrites, MAC-and-Destroy and counters come in later phases. Nothing here has been run on a spare chip.

## Use

```sh
tropicctl info chip                    # chip identification
tropicctl info fw                      # firmware version bytes
tropicctl session test                 # which pairing key opens a session
tropicctl rng get 16                   # 16 random bytes as hex
tropicctl mem write 5 --in cert.der    # R-MEM slot 5 (refuses an occupied slot unless --replace)
tropicctl mem read 5 --out got.der
tropicctl pairing list                 # the four pairing slots
tropicctl config dump                  # every R-config and I-config object
tropicctl --json pairing list          # the same, as JSON
tropicctl help mem write               # built-in help
```

Every command has a safety class (R read-only, W reversible write, I irreversible, H hazardous). Irreversible and
hazardous commands will need a named `--yes-...` flag; `--dry-run` shows what a write would do. See
[docs/safety.md](docs/safety.md) and the generated [reference](docs/reference.md).

Started under the name `lt-util` the program accepts the old `lt-util` command line (legacy mode); see SPEC.md,
section 11.

## Build

For a real chip, against a libtropic 4.2 source tree (OpenSSL crypto layer, Linux SPI layer):

```sh
cmake -S . -B build -DLIBTROPIC_SRC=/path/to/libtropic
cmake --build build
```

For tests and development without a chip (the hand-written fake):

```sh
cmake -S . -B build-fake -DTROPICCTL_CHIP=fake && cmake --build build-fake
tests/run.sh build-fake/tropicctl
```

Inside this repository's OpenWrt feed the `Makefile` builds it as the package `tropicctl`.

## Tests

`tests/run.sh` builds the fake-chip variant and runs the command and safety tests (JSON, exit codes, refusals, legacy
mode, access rights, failure handling). The fake labels each rule it models as measured on a chip or assumed;
hardware runs are described in [tests/hardware.md](tests/hardware.md). `tests/gen-docs.sh` regenerates the
documentation, and the tests fail if the committed documentation differs from the command table.

## Layout

`src/` (one file per job, see SPEC.md section 3), `tests/`, `docs/`, `CMakeLists.txt`, the OpenWrt `Makefile`.
Nothing in this directory refers to files outside it, except the libtropic source tree passed at build time, so it can
be moved to its own repository.

## Licence

BSD 3-Clause, see [LICENSE](LICENSE).
