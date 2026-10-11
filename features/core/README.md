# core — TROPIC01 platform layer

Not a CRA feature by itself: this is the shared platform every CRA feature
in this repo builds on. It turns a TROPIC01 secure element wired to the
router into a standard PKCS#11 token that other software (OpenVPN,
`cra-03-key-storage`'s AWS IoT client, OpenSSL) can use without knowing
anything about the chip.

```
Application (OpenVPN, tropic01-aws, openssl pkeyutl, ...)
    │  PKCS#11 v2.40 Cryptoki API
    ▼
libtropic_pkcs11.so  ← this feed's `libtropic-pkcs11` package
    │  Linux SPI HAL (patched in from the USB-dongle HAL upstream ships)
    ▼
/dev/spidev2.0  +  /dev/gpiochip0 pin 25  (chip-select)
    ▼
TROPIC01-ES  (Mikroe Click board, 3.3 V, 1 MHz SPI)
    ├── ECC slot 0    → P-256 private key (hardware-generated, non-exportable)
    └── R-MEM slot 1  → X.509 certificate for that key (DER, ≤ 444 bytes: the PKCS#11 module reads at most 444; `tropicctl` handles up to 475)
```

`tropicctl` is the command-line tool used to provision and inspect a chip: ECC keys, R-MEM, pairing keys, the chip
configuration, the PIN scheme (MAC-and-Destroy), counters, firmware update and chip restart. Every command is listed in
[tropicctl/docs/reference.md](tropicctl/docs/reference.md). The provisioning walkthrough is in
[../cra-03-key-storage/docs/](../cra-03-key-storage/docs/); [docs/](docs/) has the PKCS#11 data operations. See the
`.claude/skills/tropic01-pkcs11/` skill for build and provisioning troubleshooting.

## At a glance

| | |
|---|---|
| Purpose | Shared platform: makes TROPIC01 usable as a standard PKCS#11 token and from the command line |
| CRA requirement | none by itself; every CRA feature that uses the chip builds on it |
| Status | working; verified on an OpenWrt One |

## Role of TROPIC01

This folder is where the chip is *connected*: the Linux SPI driver glue, the secure-channel
handshake with the chip (libtropic), the PKCS#11 module that exposes its ECC key slots and
R-MEM, and `tropicctl` for provisioning. It does not add a security property on its own. What the
chip does for the features above it: it generates and holds P-256 private keys that never leave
it, signs with them, stores small data (certificates, the vault key) in R-MEM, and supplies
hardware random numbers. It is **not** the boot root of trust and has no influence on which
ports are open.

> **`tropicctl`** replaces the patched `lt-util`: see [tropicctl/README.md](tropicctl/README.md) and its
> [specification](tropicctl/SPEC.md). The package also installs an `lt-util` symlink that accepts the old command line.

## Packages

| Package | Purpose |
|---|---|
| `libtropic` | The one pinned libtropic 4.2.0 source (secure channel, L2/L3 protocol) that the two packages below build against |
| `libtropic-pkcs11` | PKCS#11 v2.40 Cryptoki module (`libtropic_pkcs11.so`) |
| `tropicctl` | Command-line tool: chip and firmware info, random numbers, ECC keys, R-MEM, pairing keys, configuration, PIN (MAC-and-Destroy), counters, firmware update, chip restart; installed as `lt-util` too (compatibility mode) |
| `pkcs11-helper` | Library OpenVPN uses to load and talk to a PKCS#11 module |
| `pkcs11-provider` | OpenSSL 3.x PKCS#11 provider (used by `cra-03-key-storage`'s AWS IoT client) |
| `opensc` | `pkcs11-tool` for slot inspection — **known to fail to build**, see below |
| `pkcs11-test` | Minimal PKCS#11 smoke-test utility (buildable, not selected by default) |
| `libtropic-pkcs11-test` | More thorough PKCS#11 test utility (buildable, not selected by default) |

None of `opensc`/`pkcs11-test`/`libtropic-pkcs11-test` are selected in
`openwrt.config` — build them one-off with `make package PKG=<name>` when
you need them.

## `libtropic-pkcs11` patches

| # | Patch | Purpose |
|---|---|---|
| 1 | `001-use-linux-spi-hal.patch` | Replaces the USB-dongle HAL with a Linux `/dev/spidevN.N` HAL |
| 2 | `002-use-spi-device-structure.patch` | Injects the SPI device/GPIO paths at CMake build time |
| 3 | `003-use-engineering-sample-keys.patch` | Default slot-0 pairing key is the published `ENGINEERING_SAMPLES_01` key (009 can switch it to prod0) — **see "What this does NOT fix"** |
| 4 | `004-add-cko-certificate-from-r-mem.patch` | Exposes the R-MEM cert as a `CKO_CERTIFICATE` object |
| 6 | `006-add-syslog-tracing.patch` | Adds syslog output for debugging |
| 7 | `007-load-per-unit-pairing-key.patch` | Loads a per-unit pairing key instead of the shared engineering-sample key, when one is provisioned |
| 8 | `008-port-to-libtropic-4.2.patch` | Builds against the feed's one libtropic 4.2.0 source tree; signs with libtropic 4.x `lt_ecc_ecdsa_sign()` (32-byte hash, no second hash) |
| 9 | `009-select-slot0-key-at-runtime.patch` | Chooses the slot-0 key at run time: es01 (default) or prod0, from `$TROPICCTL_SH0` or `/etc/tropic01/slot0-key`; a per-unit key (007) still takes precedence |

There is no patch 005 any more: it added a pre-hash signing call to the old bundled libtropic, which 4.x provides natively. A digest longer than 32 bytes passed to `CKM_ECDSA` is cut to its leftmost 32 bytes, a shorter one is refused with `CKR_DATA_LEN_RANGE`.

`libtropic` is built from **one** pinned source (v4.2.0, package `libtropic`). `libtropic-pkcs11` and `tropicctl` no longer bundle their own copies: they build against the tree that package installs, with the OpenSSL crypto layer and the Linux SPI hardware layer.

## R-MEM / ECC slot layout

| Area | Index | Content |
|---|---|---|
| ECC key slots | 0 | P-256 private key (hardware-generated, never exported) |
| ECC key slots | 1–31 | Reserved for additional keys |
| R-MEM | 0 | Not used (documented as read-protected on `ENGINEERING_SAMPLES_01`; on the bench chip it read back slot 1's data, backlog 3.14) |
| R-MEM | 1 | DER X.509 client cert for ECC slot 0 (≤ 444 bytes, the PKCS#11 module's limit) |
| R-MEM | 2–31 | DER certs for ECC slots 1–30, if used |
| R-MEM | 32–511 | General-purpose user data |

## Building

```sh
make package PKG=libtropic-pkcs11   # or any package name above, verbose
```

The full image build (`make`) includes everything this feature's
`openwrt.config` selects.

### "No space left on device" when installing a package post-boot

That means a package was installed at runtime (`apk add`) against a live
router's overlay, which is small. It is not an issue for anything selected
with `=y` in a config fragment: those build straight into the read-only
squashfs base at image-build time, no runtime install needed. If you hit
this, check whether the package you wanted is actually selected in
`openwrt.config` rather than trying to install it after the fact.

## What this does NOT fix

- **Shared pairing key.** A unit without a per-unit key talks to its chip
  with the published slot-0 pairing key (`ENGINEERING_SAMPLES_01`, or prod0
  via `/etc/tropic01/slot0-key`). Anyone with that key and bus access to a
  unit can open a secure session with it. `007-load-per-unit-pairing-key.patch`
  loads a per-unit key from `/etc/tropic01/pairing.{key,pub,slot}` when one
  is provisioned. Provisioning that key and taking the shared key's rights
  away is [cra-04-access-control](../cra-04-access-control/)'s job
  (`tropic01-pairing`), not done here.
- **`opensc` doesn't build.** It fails against OpenWrt's fortify headers
  (`__orig_strlcpy is not a function`). Not worth chasing — `pkcs11-test`
  covers the same verification need.
