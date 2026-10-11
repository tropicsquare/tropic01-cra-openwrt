---
name: tropic01-pkcs11
description: >
  Build, provision, and troubleshoot this OpenWrt package feed that adds TROPIC01 secure-element
  support (libtropic, libtropic-pkcs11, tropicctl, coreMQTT/AWS IoT, pkcs11-provider).
  Use when: building firmware with TROPIC01/PKCS#11 packages, generating or exporting
  hardware-backed P-256 keys, provisioning X.509 certs into TROPIC01 R-MEM, wiring
  OpenVPN or AWS IoT to the PKCS#11 module, fixing "no space on device" firmware issues,
  debugging SPI/tropicctl errors, performing PKCS#11 data operations (sign, verify, encrypt,
  decrypt, digest, random), or extending libtropic-pkcs11 patches.
---

# TROPIC01 Secure Element Integration (OpenWrt)

This repo adds hardware-backed key storage via the **TROPIC01** secure element
(Tropic Square), connected over SPI (or USB dongle), exposed to userspace through a
PKCS#11 v2.40 module. It backs OpenVPN client auth with a private key that never leaves the
chip (AWS IoT device identity is not working yet; backlog 3.1).

---

## Architecture

```
OpenVPN / AWS IoT (MQTT/TLS)
    │  pkcs11-providers  /usr/lib/libtropic_pkcs11.so
    │  pkcs11-id         TropicSquare/TROPIC01-ES/<serial>/<label>/00
    ▼
libtropic_pkcs11.so  ← PKCS#11 v2.40 Cryptoki module (libtropic-pkcs11)
    │  Linux SPI HAL  (patched from USB dongle HAL)
    ▼
/dev/spidev2.0  +  /dev/gpiochip0 pin 25
    ▼
TROPIC01-ES  (Mikroe Click board, 3.3 V, 1 MHz SPI)
    ├── ECC slot 0   → P-256 private key (hardware-generated, non-exportable)
    └── R-MEM slot 1 → X.509 client certificate (DER, up to 444 bytes: the PKCS#11 module's limit; `tropicctl` handles 475)
```

---

## Key Packages (`features/core/`)

| Package | Purpose |
|---|---|
| `libtropic` | Base library for talking to TROPIC01 (secure channel, L2/L3 protocol) |
| `libtropic-pkcs11` | PKCS#11 v2.40 Cryptoki module (`libtropic_pkcs11.so`) — has patches 001–009 (no 005), see below |
| `tropicctl` | Command-line tool: chip and firmware info, firmware update, chip restart, random numbers, ECC keys, R-MEM, pairing keys, configuration, PIN (MAC-and-Destroy), counters, session test; installed as `lt-util` too (compatibility mode) |
| `pkcs11-provider` / `pkcs11-helper` | OpenSSL PKCS#11 engine/provider glue used by OpenVPN |
| `coreMQTT` | AWS IoT MQTT client (`features/cra-03-key-storage/`; see `aws/README.md` and `docs/solution-description.md`) |

Additional packages:

| Package | Location | Purpose |
|---|---|---|
| `openvpn` | `features/cra-03-key-storage/openvpn` | OpenVPN with PKCS#11 support + TROPIC01 procd tuning |
| `tropic01-aws` | `features/cra-03-key-storage/tropic01-aws` | AWS IoT Core MQTT client over TROPIC01 mTLS (not working today; backlog 3.1) |
| `opensc` | `features/core/opensc` | `pkcs11-tool` for slot inspection and key management (build may fail) |
| `pkcs11-test` | `features/core/pkcs11-test` | Minimal PKCS#11 smoke-test utility |
| `libtropic-pkcs11-test` | `features/core/libtropic-pkcs11-test` | More thorough PKCS#11 test utility (not selected by default) |
| `tropic01-pairing` | `features/cra-04-access-control/tropic01-pairing` | Guarded per-unit pairing key provisioning on top of `tropicctl` (status, provision, undo) |

---

## `libtropic-pkcs11` Patches

Located in [`features/core/libtropic-pkcs11/patches/`](../../../features/core/libtropic-pkcs11/patches/):

| # | Patch | Purpose |
|---|---|---|
| 1 | `001-use-linux-spi-hal.patch` | Replaces USB-dongle HAL with Linux `/dev/spidevN.N` HAL |
| 2 | `002-use-spi-device-structure.patch` | Injects SPI device/GPIO paths at CMake build time |
| 3 | `003-use-engineering-sample-keys.patch` | Pairing keys for `ENGINEERING_SAMPLES_01` silicon |
| 4 | `004-add-cko-certificate-from-r-mem.patch` | Exposes R-MEM cert as a `CKO_CERTIFICATE` object |
| 6 | `006-add-syslog-tracing.patch` | Adds syslog output for debugging (debug aid; see backlog 3.10) |
| 7 | `007-load-per-unit-pairing-key.patch` | Loads a per-unit pairing key instead of the shared engineering-sample key, when one is provisioned |
| 8 | `008-port-to-libtropic-4.2.patch` | Builds against the feed's libtropic 4.2.0 tree; signs a 32-byte hash with `lt_ecc_ecdsa_sign()` (no second hash) |
| 9 | `009-select-slot0-key-at-runtime.patch` | Chooses the slot-0 key (`es01` default, or `prod0`) at run time from `$TROPICCTL_SH0` or `/etc/tropic01/slot0-key`; a per-unit key (007) still wins |

`tropicctl` ([features/core/tropicctl/](../../../features/core/tropicctl/), specification in `SPEC.md`) replaces the patched `lt-util`; it is its own source tree with no patches. A P-256 key signs through `tropicctl ecc sign` (ECDSA, with OpenSSL-compatible DER output) or through PKCS#11 / OpenSSL.

---

## Hardware Wiring Convention

| Signal | Router assignment |
|---|---|
| SPI bus | `/dev/spidev2.0` at 1 MHz |
| Chip-select | `/dev/gpiochip0` GPIO pin 25 |
| Supply | 3.3 V |

Slot convention: ECC key slot `N` pairs with R-MEM cert slot `N+1` (R-MEM slot 0 is
not used: documented as read-protected on `ENGINEERING_SAMPLES_01`, but it read back slot 1's data
on the bench chip, backlog 3.14).

### R-MEM / ECC Slot Layout

| Area | Index | Content |
|---|---|---|
| ECC key slots | 0 | P-256 private key (hardware-generated, never exported) |
| ECC key slots | 1–31 | Additional keys; slot N is PKCS#11 id `%0N` |
| R-MEM | 0 | **READ-PROTECTED** on ENGINEERING_SAMPLES_01 — do not use |
| R-MEM | 1 | DER X.509 client cert for ECC slot 0 (≤ 444 bytes, the PKCS#11 module's limit) |
| R-MEM | 2–31 | DER certs for ECC slots 1–30 (if used) |
| R-MEM | 32–511 | General-purpose user data |

---

## Build Procedure

### Prerequisites

- Linux build host (Debian/Ubuntu recommended), or Docker
- Standard OpenWrt build dependencies (see root [README.md](../../../README.md) quick start)
- TROPIC01-ES evaluation hardware (for hardware-in-the-loop tests)

### Quick Build

This repo is a package feed + build harness, not a full OpenWrt checkout
(see root [README.md](../../../README.md)). `make` clones the pinned
upstream OpenWrt, applies patches, assembles `.config` from
`configs/openwrt-one.config` plus every `features/*/openwrt.config`
fragment, and builds:

```bash
make                                 # full image
make package PKG=libtropic           # one package, verbose
make package PKG=libtropic-pkcs11
make package PKG=tropicctl
make package PKG=openvpn
make menuconfig                      # change the assembled .config interactively
```

`opensc` and `pkcs11-test` are not selected by default (see Known Pitfalls
below); build them one-off with `make package PKG=<name>` when needed.

### SPI Configuration (CMake)

Defaults in [`features/core/libtropic-pkcs11/Makefile`](../../../features/core/libtropic-pkcs11/Makefile):

```makefile
CMAKE_OPTIONS += \
    -DTS_SPI_DEV="/dev/spidev2.0" \
    -DTS_GPIO_DEV="/dev/gpiochip0" \
    -DTS_GPIO_CS_PIN=25 \
    -DTS_SPI_SPEED=1000000 \
    -DLT_SH0_KEYS="eng_sample" \
    -DLT_PKCS11_LOG_EN=ON
```

Change `TS_SPI_DEV`, `TS_GPIO_DEV`, or `TS_GPIO_CS_PIN` for different boards.

The slot-0 key is chosen at run time (patch 009): `es01` unless `$TROPICCTL_SH0` or `/etc/tropic01/slot0-key` says `prod0`.

### Build Outputs

- `build/openwrt/build_dir/target-aarch64_cortex-a53_musl/libtropic-pkcs11-*/libtropic_pkcs11.so` (151KB)
- `build/openwrt/bin/targets/mediatek/filogic/openwrt-mediatek-filogic-openwrt_one-squashfs-sysupgrade.itb`

### "No space left on device" when installing packages post-boot

This means the overlay is too small for a package installed (`apk add`) after
flashing. It is not an issue for anything this feed selects with `=y` in a
config fragment: those build straight into the read-only squashfs base, no
runtime install or `files/` overlay copying needed. See
`features/core/README.md` for the full note.

---

## Provisioning Flow (Hardware Key + Cert)

All provisioning steps run **on the router** over SSH.

### 1. Verify SPI Connectivity

```bash
tropicctl info chip
# There is no separate "connectivity" flag — `info chip` prints the chip
# identification block; a successful read confirms SPI/GPIO wiring and
# pairing keys both work. Expected: lines like
#   P/N (long)  = 0x0B54524F50494330312D4553FFFFFFFF (TROPIC01-ES)
#   Batch ID    = 0x0000FFFFFF
# "GPIO_V2_GET_LINE_IOCTL error! ... Resource busy" means another process
# (usually OpenVPN) already holds the single SPI/GPIO session — stop it
# first (/etc/init.d/openvpn stop).
```

### 2. Generate Hardware Key

```bash
tropicctl ecc generate 0 --curve p256
# Expected: ecc slot 0: generated a p256 key; public key <hex>
```

The private key is generated inside TROPIC01 and **cannot be exported**.

### 3. Export Public Key

```bash
tropicctl ecc pub 0 --out /tmp/tropic_pub.bin
# Writes the 64-byte X||Y public key; nothing is printed
```

Copy to build host: `scp root@<router-ip>:/tmp/tropic_pub.bin /tmp/`

### 4. Create the CSR, sign it, store the certificate, install the profile

Do not hand-build an SPKI or use `-force_pubkey`: the CSR is made *by the chip*
through OpenSSL's `pkcs11-provider` (`pkcs11:id=%00;type=private`), so it carries
a real proof of possession. Follow the verified step-by-step guide:
[`openvpn-user-guide.md`](../../../features/cra-03-key-storage/docs/openvpn-user-guide.md).
To install a profile without editing configs by hand, use
[`tropic01-install-ovpn.sh`](../../../features/cra-03-key-storage/host-tools/tropic01-install-ovpn.sh).

Rules that apply to every step that writes the chip:

- Key generation (`tropicctl ecc generate N --curve p256`) and certificate storage (`tropicctl mem write N+1 --in file`)
  are irreversible for that slot: get explicit user confirmation first.
- Every router needs a unique certificate CN.
- Never commit a filled-in `.ovpn`, certificates or keys.
- AWS IoT enrolment is not part of cra-03 today; see
  [`aws-iot-known-issues.md`](../../../features/cra-03-key-storage/docs/aws-iot-known-issues.md)
  and backlog 3.1.

---

## Testing the PKCS#11 Module

### Build and Deploy pkcs11-test

```bash
make package PKG=pkcs11-test

# Deploy to router
# copy the built pkcs11-test package from build/openwrt/bin/packages/aarch64_cortex-a53/tropic01/ to the router and install it with the package manager
ssh root@<router-ip> 'chmod +x /usr/bin/pkcs11-test'
```

### Run Smoke Test

```bash
ssh root@<router-ip> 'pkcs11-test /usr/lib/libtropic_pkcs11.so'
```

Expected output:
```
PKCS#11 Module Test
==================
Loading module: /usr/lib/libtropic_pkcs11.so
✓ Module loaded successfully
✓ C_GetFunctionList succeeded
✓ C_Initialize succeeded
Module Information:
  Cryptoki Version: 2.40
  Manufacturer ID:  Tropic Square
  Library Description: Tropic PKCS#11 Module
  Library Version:  0.1
✓ C_Finalize succeeded
All tests passed!
```

### Additional Ad-Hoc Test Files

In `features/core/tests/` — router-side diagnostics, not built by the
feed (see that folder's README):

- `test-pkcs11-full.c` — comprehensive PKCS#11 test
- `test-pkcs11-info.c` — module info query
- `test-slot-list.c` — slot enumeration
- `test-pkcs11-comprehensive.sh` — shell-based test
- `quick-test.sh` — quick smoke test

---

## PKCS#11 Data Operations Reference

See [`features/core/docs/pkcs11-data-operations.md`](../../../features/core/docs/pkcs11-data-operations.md) for full C code examples. Quick reference:

### Query Slots and Tokens

```c
CK_ULONG slotCount;
C_GetSlotList(CK_TRUE, NULL, &slotCount);
CK_SLOT_ID slots[slotCount];
C_GetSlotList(CK_TRUE, slots, &slotCount);
CK_SLOT_INFO slotInfo;
C_GetSlotInfo(slots[0], &slotInfo);
CK_TOKEN_INFO tokenInfo;
C_GetTokenInfo(slots[0], &tokenInfo);
```

### List Objects

```c
CK_SESSION_HANDLE session;
C_OpenSession(slotId, CKF_SERIAL_SESSION, NULL, NULL, &session);
C_FindObjectsInit(session, NULL, 0);
CK_OBJECT_HANDLE objects[100]; CK_ULONG objectCount;
C_FindObjects(session, objects, 100, &objectCount);
C_FindObjectsFinal(session);
// Use C_GetAttributeValue to read CKA_CLASS, CKA_LABEL, CKA_VALUE, etc.
```

### Sign / Verify

```c
CK_MECHANISM mech = {CKM_ECDSA, NULL, 0};
C_SignInit(session, &mech, privateKeyHandle);
C_Sign(session, data, dataLen, signature, &sigLen);
C_VerifyInit(session, &mech, publicKeyHandle);
C_Verify(session, data, dataLen, signature, sigLen);
```

### Digest / Random

```c
CK_MECHANISM mech = {CKM_SHA256, NULL, 0};
C_DigestInit(session, &mech);
C_Digest(session, data, dataLen, hash, &hashLen);
C_GenerateRandom(session, randomData, sizeof(randomData));
```

### Common PKCS#11 Return Codes

| Code | Constant | Meaning |
|---|---|---|
| `0x00000000` | `CKR_OK` | Success |
| `0x00000030` | `CKR_DEVICE_ERROR` | Device not connected |
| `0x000000E1` | `CKR_DEVICE_REMOVED` | Device unplugged |
| `0x000000E0` | `CKR_TOKEN_NOT_PRESENT` | No token in slot |
| `0x00000054` | `CKR_FUNCTION_NOT_SUPPORTED` | Operation not supported |
| `0x00000060` | `CKR_KEY_HANDLE_INVALID` | Invalid key handle |
| `0x00000070` | `CKR_MECHANISM_INVALID` | Unsupported mechanism |
| `0x000000A0` | `CKR_PIN_INCORRECT` | Wrong PIN |
| `0x000000B0` | `CKR_SESSION_CLOSED` | Session not open |
| `0x00000190` | `CKR_CRYPTOKI_NOT_INITIALIZED` | Not initialized |

---

## Known Pitfalls (Check Before Re-diagnosing)

- **"No space on device" when installing packages post-boot**: this means a
  package was installed at runtime (`apk add`) instead of selected at build time.
  Select it as `y` in the relevant `features/*/openwrt.config` fragment
  instead, so it builds into the read-only squashfs base — no `files/`
  overlay copying needed.

- **`opensc` fails to build** (`__orig_strlcpy is not a function`): Conflicts with
  OpenWrt fortify headers. Not worth fixing — use `pkcs11-test` for verification instead.

- **OpenVPN TLS handshake fails / signature invalid**: Check that the module
  signs the 32-byte digest OpenVPN passes via `CKM_ECDSA` directly (patch
  `008-port-to-libtropic-4.2.patch`); a second hash makes every signature invalid.

- **`LT_L2_SPI_ERROR` from `tropicctl`**: Check physical SPI/GPIO wiring.
  Verify `/dev/spidev2.0` and `/dev/gpiochip0` exist on the target.

- **Secure session refused (`LT_CRYPTO_ERR`)**: the slot-0 key does not match the chip. A chip with
  production provisioning needs `prod0` (`/etc/tropic01/slot0-key`, `$TROPICCTL_SH0` or
  `tropicctl --key prod0`). On a provisioned unit the shared key is restricted (exit 5); use the
  per-unit key in `/etc/tropic01/`.

- **`LT_L3_R_MEM_DATA_WRITE_WRITE_FAIL` on slot 0**: R-MEM slot 0 is hardware-protected
  on ES01 silicon. Use slot 1+.

- **`Cannot open TUN/TAP dev /dev/net/tun`**: `tun` module not loaded.
  Fix: `modprobe tun && echo tun > /etc/modules.d/tun`.

- **`ip link set failed: could not execute external program`**: OpenVPN looking for
  `/usr/sbin/ip`. Fix: add `iproute /sbin/ip` to client config.

- **Two clients get the same VPN IP**: Same CN on both certs.
  Fix: use unique CN per device when signing.

---

## Security Properties

- TROPIC01 private keys are generated inside the secure element and **never exported** in plaintext.
- All mTLS connections use P-256 keys with certificates issued by a private CA.
- AWS IoT is meant to use mutual TLS on port 8883; it does not work today (backlog 3.1).
- OpenVPN uses `tls-crypt` (HMAC firewall) to prevent unauthenticated connection attempts.
- Chip serial is embedded in PKCS#11 token label, binding VPN config to one specific physical chip.

### Secret Handling

This repository intentionally **does not** store credentials. Excluded via
the root [`.gitignore`](../../../.gitignore): private keys and certificates
(`*.key`, `*.pem.crt`, `*.csr`, …), filled-in VPN profiles (`*.ovpn` — the
committed templates end in `.example`), and per-unit credential manifests
(`*manifest*.csv`).

To report a vulnerability: **security@tropicsquare.com** (see
[`SECURITY.md`](../../../SECURITY.md)).

---

## Configuration File Reference (on Router)

| File | Purpose |
|---|---|
| `/etc/openvpn/tropic01-pkcs11-client.conf` | OpenVPN client config (PKCS#11 auth) |
| `/etc/openvpn/ca.crt` | VPN server CA certificate |
| `/etc/openvpn/ta.key` | TLS static key (HMAC firewall) |
| `/etc/openvpn/client.crt` | Client X.509 certificate (public, for reference) |
| `/etc/config/openvpn` | UCI config — enables procd autostart at boot |
| `/etc/ssl/tropic01-aws/openssl-aws.cnf` | OpenSSL PKCS#11 provider config |
| `/etc/ssl/tropic01-aws/AmazonRootCA1.pem` | Amazon Root CA for AWS IoT TLS |
| `/etc/ssl/tropic01-aws/device.pem` | Device certificate (PEM) |
| `/etc/config/tropic01-aws` | UCI config — endpoint, topic, interval |
| `/usr/bin/tropicctl` (and the `lt-util` symlink) | TROPIC01 command-line tool |
| `/usr/lib/libtropic_pkcs11.so` | PKCS#11 provider |

---

## Contributing

See [`CONTRIBUTING.md`](../../../CONTRIBUTING.md) for full guidelines. Quick reference:

- **C code**: K&R braces, tabs for indentation, 80-column soft limit
- **Shell scripts**: POSIX sh; `#!/bin/sh`; `set -eu`; use `logger` for logging
- **Makefiles**: Follow OpenWrt package conventions; use `PKG_VERSION`, `PKG_HASH`, `PKG_SOURCE_URL`
- **Patches**: `git format-patch` style; prefix with three-digit sequence + short verb (`001-fix-…`, `002-add-…`)
- **Commits**: [Conventional Commits](https://www.conventionalcommits.org/) format
  - Types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`
  - Example: `feat(libtropic-pkcs11): add multi-session token support`

---

## Related Docs

| Document | Topic |
|---|---|
| [`features/cra-03-key-storage/docs/solution-description.md`](../../../features/cra-03-key-storage/docs/solution-description.md) | Architecture overview (VPN + AWS IoT) |
| [`features/cra-03-key-storage/docs/vpn-router-setup.md`](../../../features/cra-03-key-storage/docs/vpn-router-setup.md) | Full VPN provisioning walkthrough |
| [`features/cra-03-key-storage/docs/openvpn-pkcs11.md`](../../../features/cra-03-key-storage/docs/openvpn-pkcs11.md) | OpenVPN + PKCS#11 config details |
| [`features/core/docs/pkcs11-data-operations.md`](../../../features/core/docs/pkcs11-data-operations.md) | R-MEM/ECC slot data operations reference |
| [`CONTRIBUTING.md`](../../../CONTRIBUTING.md) | How to contribute |
| [`SECURITY.md`](../../../SECURITY.md) | Vulnerability reporting |
| [`features/cra-03-key-storage/aws/README.md`](../../../features/cra-03-key-storage/aws/README.md) | AWS IoT credential files |
| [`README.md`](../../../README.md) | Main project README |