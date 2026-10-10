# tropicctl

A command-line tool for the [TROPIC01](https://tropicsquare.com/tropic01) secure element on Linux (SPI and GPIO). It
reads chip and firmware information, gets random numbers, stores data in R-MEM, reads pairing keys and the chip
configuration, and is built so that scripts can rely on it: stable exit codes, optional JSON, data on stdout and
notices on stderr, and a safety class on every command.

**Status: phases 1 to 4 of [SPEC.md](SPEC.md) are implemented.** The read-only commands were run on a real chip. Everything
that writes the chip (ECC keys, pairing keys, configuration rewrites, MAC-and-Destroy, counters) is tested against a
hand-written fake chip only, until a spare chip is available; the fake labels each rule it is not sure of as *assumed*.

## Use

```sh
tropicctl info chip                         # chip identification (also: info fw, info mode, info cert)
tropicctl session test                      # which pairing key opens a session
tropicctl rng get 16                        # 16 random bytes as hex
tropicctl mem write 5 --in cert.der         # R-MEM slot 5 (refuses an occupied slot unless --replace)
tropicctl mem read 5 --out got.der

tropicctl ecc generate 3 --curve p256       # a key made inside the chip; the private half never leaves it
tropicctl ecc pub 3 --format pem --out k.pem
tropicctl ecc sign 3 --in msg.bin --der --out sig.der      # ECDSA over SHA-256(msg); OpenSSL verifies it

tropicctl pairing list                      # the four pairing slots
tropicctl pairing write 1 --in unit.pub --yes-write-pairing-slot   # IRREVERSIBLE: the slot is used up
tropicctl config dump --decode              # who may do what, per command and target group
tropicctl config restrict-shared --unit-slot 1 --out r.cfg         # a file, nothing written to the chip
tropicctl config rewrite --in r.cfg --yes-rewrite-r-config         # backup, erase, write, read back

tropicctl --pin-file pin.txt mnd setup --state pin.state --out k.bin --yes-setup-mnd   # a PIN with a chip-enforced limit
tropicctl --key pin session test            # a session with the PIN-protected pairing key
tropicctl counter get 0                     # monotonic counters
tropicctl fw status                         # firmware versions, silicon revision, mode
tropicctl --key prod0 session test          # chips with production provisioning use the prod0 slot-0 key
tropicctl chip reboot                       # load a rewritten R-config (it applies only after a restart)
tropicctl --json pairing list               # JSON for scripts
tropicctl help mem write                    # built-in help
```

Every command has a safety class (R read-only, W reversible write, I irreversible, H hazardous). Irreversible and
hazardous commands need a named `--yes-...` flag and refuse without it before touching the chip; `--dry-run` runs every
check and shows what a write would do. See
[docs/safety.md](docs/safety.md) and the generated [reference](docs/reference.md).

Started under the name `lt-util` (the package installs that symlink) the program accepts the whole old `lt-util`
command line, with its old messages and exit status 0 or 1 (legacy mode); see SPEC.md, section 11.

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

`tests/run.sh` builds the fake-chip variant and runs the command and safety tests (about 370: JSON, exit codes, refusals,
legacy mode, access rights, failure handling, ECC signatures verified by OpenSSL, the PIN scheme and the PIN-protected
session). Every irreversible and hazardous guard has been checked by breaking it and seeing a test fail. The fake needs
OpenSSL (libssl-dev) on the host. The fake labels each rule it models as measured on a chip or assumed;
hardware runs are described in [tests/hardware.md](tests/hardware.md). `tests/gen-docs.sh` regenerates the
documentation, and the tests fail if the committed documentation differs from the command table.

## Layout

`src/` (one file per job, see SPEC.md section 3), `tests/`, `docs/`, `CMakeLists.txt`, the OpenWrt `Makefile`.
Nothing in this directory refers to files outside it, except the libtropic source tree passed at build time, so it can
be moved to its own repository.

## Licence

BSD 3-Clause, see [LICENSE](LICENSE).

## Which slot-0 key a chip answers to

Every chip ships with a shared key in pairing slot 0, published with libtropic. Engineering samples use the ES01 key;
chips with production provisioning use the prod0 key and refuse ES01 (`secure session failed: LT_CRYPTO_ERR`).
`tropicctl info chip` and `fw status` work either way, because they need no session. To say once which one a unit has:

```sh
echo prod0 > /etc/tropic01/slot0-key     # or es01 (the default); TROPICCTL_SH0=prod0 overrides it for one command
```

`--key shared` and the fallback used when no per-unit key exists follow that setting. The `libtropic-pkcs11` module
(OpenVPN through the chip) reads the same file, so one setting covers both. An unknown name is an error, not a silent
fallback. A build can change the default with `TROPICCTL_DEFAULT_SH0=prod0`.
