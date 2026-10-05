---
name: tropic01-pkcs11
description: >
  Build, provision, and troubleshoot this OpenWrt package feed that adds TROPIC01 secure-element
  support (libtropic, libtropic-pkcs11, lt-util, coreMQTT/AWS IoT, pkcs11-provider).
  Use when: building firmware with TROPIC01/PKCS#11 packages, generating or exporting
  hardware-backed P-256 keys, provisioning X.509 certs into TROPIC01 R-MEM, wiring
  OpenVPN or AWS IoT to the PKCS#11 module, fixing "no space on device" firmware issues,
  debugging SPI/lt-util errors, performing PKCS#11 data operations (sign, verify, encrypt,
  decrypt, digest, random), or extending libtropic-pkcs11 patches.
---

# TROPIC01 Secure Element Integration (OpenWrt)

This repo adds hardware root-of-trust support via the **TROPIC01-ES** secure element
(Tropic Square), connected over SPI (or USB dongle), exposed to userspace through a
PKCS#11 v2.40 module. It backs OpenVPN client auth and AWS IoT device identity with a
private key that never leaves the chip.

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
    └── R-MEM slot 1 → X.509 client certificate (DER, up to 444 bytes)
```

---

## Key Packages (`features/core/`)

| Package | Purpose |
|---|---|
| `libtropic` | Base library for talking to TROPIC01 (secure channel, L2/L3 protocol) |
| `libtropic-pkcs11` | PKCS#11 v2.40 Cryptoki module (`libtropic_pkcs11.so`) — has patches 001–006, see below |
| `lt-util` | CLI provisioning tool: generate keys, read pubkey, write/read R-MEM slots |
| `pkcs11-provider` / `pkcs11-helper` | OpenSSL PKCS#11 engine/provider glue used by OpenVPN |
| `coreMQTT` | AWS IoT MQTT client (`features/cra-03-key-storage/`; see `aws/README.md` and `docs/solution-description.md`) |

Additional packages:

| Package | Location | Purpose |
|---|---|---|
| `openvpn` | `features/cra-03-key-storage/openvpn` | OpenVPN with PKCS#11 support + TROPIC01 procd tuning |
| `tropic01-aws` | `features/cra-03-key-storage/tropic01-aws` | AWS IoT Core MQTT client — publishes telemetry via TROPIC01 mTLS |
| `opensc` | `features/core/opensc` | `pkcs11-tool` for slot inspection and key management (build may fail) |
| `pkcs11-test` | `features/core/pkcs11-test` | Minimal PKCS#11 smoke-test utility |

---

## `libtropic-pkcs11` Patches

Located in [`features/core/libtropic-pkcs11/patches/`](../../../features/core/libtropic-pkcs11/patches/):

| # | Patch | Purpose |
|---|---|---|
| 1 | `001-use-linux-spi-hal.patch` | Replaces USB-dongle HAL with Linux `/dev/spidevN.N` HAL |
| 2 | `002-use-spi-device-structure.patch` | Injects SPI device/GPIO paths at CMake build time |
| 3 | `003-use-engineering-sample-keys.patch` | Pairing keys for `ENGINEERING_SAMPLES_01` silicon |
| 4 | `004-add-cko-certificate-from-r-mem.patch` | Exposes R-MEM cert as a `CKO_CERTIFICATE` object |
| 5 | `005-add-ecdsa-sign-prehash.patch` | Adds `lt_ecc_ecdsa_sign_prehash()` — **critical**: avoids double-hashing pre-hashed digests from OpenVPN/`CKM_ECDSA` |
| 6 | `006-add-syslog-tracing.patch` | Adds syslog output for debugging (debug aid; see backlog 3.10) |
| 7 | `007-load-per-unit-pairing-key.patch` | Loads a per-unit pairing key instead of the shared engineering-sample key, when one is provisioned |

`lt-util` carries its own patches in [`features/core/lt-util/patches/`](../../../features/core/lt-util/patches/) (SPI device, engineering-sample keys, P-256 and full public-key export, per-unit pairing key). `lt-util -e -s` signs with EdDSA only and fails on a P-256 slot: sign through PKCS#11 / OpenSSL instead.

---

## Hardware Wiring Convention

| Signal | Router assignment |
|---|---|
| SPI bus | `/dev/spidev2.0` at 1 MHz |
| Chip-select | `/dev/gpiochip0` GPIO pin 25 |
| Supply | 3.3 V |

Slot convention: ECC key slot `N` pairs with R-MEM cert slot `N+1` (R-MEM slot 0 is
read-protected on `ENGINEERING_SAMPLES_01` silicon).

### R-MEM / ECC Slot Layout

| Area | Index | Content |
|---|---|---|
| ECC key slots | 0 | P-256 private key (hardware-generated, never exported) |
| ECC key slots | 1–31 | Additional keys; slot N is PKCS#11 id `%0N` |
| R-MEM | 0 | **READ-PROTECTED** on ENGINEERING_SAMPLES_01 — do not use |
| R-MEM | 1 | DER X.509 client cert for ECC slot 0 (≤ 444 bytes) |
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
make package PKG=lt-util
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

### Build Outputs

- `build/openwrt/build_dir/target-aarch64_cortex-a53_musl/libtropic-pkcs11-*/libtropic_pkcs11.so` (151KB)
- `build/openwrt/bin/targets/mediatek/filogic/openwrt-mediatek-filogic-openwrt_one-squashfs-sysupgrade.itb`

### "No space left on device" when installing packages post-boot

This means the overlay is too small for an `opkg install` done after
flashing. It is not an issue for anything this feed selects with `=y` in a
config fragment: those build straight into the read-only squashfs base, no
runtime install or `files/` overlay copying needed. See
`features/core/README.md` for the full note.

---

## Provisioning Flow (Hardware Key + Cert)

All provisioning steps run **on the router** over SSH.

### 1. Verify SPI Connectivity

```bash
lt-util -i
# There is no separate "connectivity" flag — -i prints the chip
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
lt-util -e -g 0
# Expected: lt_ecc_key_generate() : LT_OK
```

The private key is generated inside TROPIC01 and **cannot be exported**.

### 3. Export Public Key

```bash
lt-util -e -d 0 /tmp/tropic_pub.bin
# Expected: Curve: P256, Number of elements written: 64
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

- Key generation (`lt-util -e -g N`) and certificate storage (`lt-util -m -s N+1 file`)
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
scp build/openwrt/bin/targets/mediatek/filogic/packages/pkcs11-test root@<router-ip>:/usr/bin/
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
  package was `opkg install`ed at runtime instead of selected at build time.
  Select it as `y` in the relevant `features/*/openwrt.config` fragment
  instead, so it builds into the read-only squashfs base — no `files/`
  overlay copying needed.

- **`opensc` fails to build** (`__orig_strlcpy is not a function`): Conflicts with
  OpenWrt fortify headers. Not worth fixing — use `pkcs11-test` for verification instead.

- **OpenVPN TLS handshake fails / signature invalid**: Likely missing patch
  `005-add-ecdsa-sign-prehash.patch` — OpenVPN passes a pre-hashed digest via
  `CKM_ECDSA`, and without this patch the module double-hashes it.

- **`LT_L2_SPI_ERROR` from `lt-util`**: Check physical SPI/GPIO wiring.
  Verify `/dev/spidev2.0` and `/dev/gpiochip0` exist on the target.

- **`LT_HOST_SECURE_CHANNEL_SETUP` error**: Pairing keys mismatch — confirm firmware
  was built with patch `003-use-engineering-sample-keys.patch` matching your chip's
  provisioning (`ENGINEERING_SAMPLES_01`).

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
- AWS IoT connections use mutual TLS on port 8883 with device certificates registered in AWS IoT Core.
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
| `/usr/bin/lt-util` | TROPIC01 provisioning tool |
| `/usr/lib/libtropic_pkcs11.so` | PKCS#11 provider |

---

## Contributing

See [`CONTRIBUTING.md`](../../../CONTRIBUTING.md) for full guidelines. Quick reference:

- **C code**: K&R braces, tabs for indentation, 80-column soft limit
- **Shell scripts**: POSIX sh; `#!/bin/sh`; `set -e`; use `logger` for logging
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