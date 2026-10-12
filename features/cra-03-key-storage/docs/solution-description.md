# Hardware-Backed VPN Router: the Client Key in TROPIC01

## What the Solution Does

This OpenWrt-based firmware turns a MediaTek MT7981B router (e.g. OpenWrt One) into a
VPN client where **the TLS private key never exists as software**. Every TLS
authentication operation uses a hardware-generated, hardware-bound P-256 key stored
inside the **TROPIC01-ES** secure element. OpenVPN and OpenSSL are used as shipped; the
PKCS#11 module carries this feed's patches. TROPIC01 is the only crypto backend that
touches the private key.

---

## Architecture

```
OpenVPN (client, TLS mutual auth)
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

## Role of TROPIC01

TROPIC01 (manufactured by Tropic Square) is a purpose-built secure element IC.
In this solution it plays the following distinct roles.

### 1. Secure Key Generation

The P-256 (secp256r1) key pair is created **inside the chip** using an on-chip true
random number generator (TRNG):

```bash
tropicctl ecc generate 0 --curve p256   # Generate P-256 key in ECC slot 0
```

The private key is placed in **ECC slot 0** and is permanently non-extractable.
Only the corresponding 64-byte raw public key (X‖Y coordinates) can be read back:

```bash
tropicctl ecc pub 0 --out /tmp/tropic_pub.bin
```

This is the primary security property: **no software path exists to read or copy
the private key**.

---

### 2. Hardware ECDSA Signing

During every OpenVPN TLS handshake, the client must sign a challenge with its private
key. The call chain is:

```
OpenVPN → PKCS#11 (CKM_ECDSA) → libtropic_pkcs11.so → lt_ecc_ecdsa_sign() → TROPIC01 ECC slot 0
```

TROPIC01 performs the raw ECDSA-P256 operation and returns only the 64-byte signature.
The private key never crosses the SPI bus in either direction.

OpenVPN passes a **pre-hashed** 32-byte SHA-256 digest via `CKM_ECDSA`. Patch
`008-port-to-libtropic-4.2.patch` passes that hash to `lt_ecc_ecdsa_sign()`, which in
libtropic 4.2 takes a 32-byte hash and does not hash again.

---

### 3. Secure Certificate Storage

The X.509 client certificate (DER-encoded, ~300 bytes) is stored in **R-MEM slot 1**
inside TROPIC01:

```bash
tropicctl mem write 1 --in /tmp/client.der   # Write DER certificate to R-MEM slot 1
```

At PKCS#11 token initialization time, patch
`004-add-cko-certificate-from-r-mem.patch` reads this slot and exposes the certificate
as a `CKO_CERTIFICATE` object. OpenVPN discovers it automatically via
`--show-pkcs11-ids`. This means both the private key **and** the identity certificate
are bound to the chip — losing or replacing the TROPIC01 chip invalidates the router's
VPN credentials entirely.

The slot convention used: **ECC slot N → R-MEM slot N+1**. R-MEM slot 0 is not used:
its behaviour on the bench chip is unclear (backlog 3.14).

---

### 4. Identity Token (Chip Serial Number)

TROPIC01's unique chip identifier is embedded in the PKCS#11 token label. This makes
each chip's token globally unique:

```
Serialized id:  TropicSquare/TROPIC01\x2DES/0000FFFFFF5453/TROPIC01\x2DES\x2D5354/00
                                            └── chip serial ──┘
```

This serial is used in the OpenVPN `pkcs11-id` directive, binding the VPN configuration
to one specific physical chip. A chip swap requires full re-provisioning.

---

### 5. Physical Interface

TROPIC01 connects via SPI on the Mikroe Click board:

| Signal | Router assignment |
|--------|-------------------|
| SPI bus | `/dev/spidev2.0` at 1 MHz |
| Chip-select | `/dev/gpiochip0` GPIO pin 25 |
| Supply | 3.3 V |

Patch `001-use-linux-spi-hal.patch` replaces the upstream USB-dongle HAL with a Linux
generic SPI HAL. Patch `002-use-spi-device-structure.patch` injects device paths at
CMake build time so no source editing is needed.

---

## Provisioning Flow

```
1. Flash firmware              → libtropic_pkcs11.so baked in
2. tropicctl ecc generate 0 --curve p256             → P-256 key pair created in TROPIC01;
                                  private key never leaves
3. tropicctl ecc pub 0 --out <file>       → export raw 64-byte public key (X‖Y)
4. Wrap in SPKI DER/PEM        → build-host Python snippet
5. openssl x509 -force_pubkey  → CA signs cert binding TROPIC01 public key to a CN
6. openssl x509 -outform DER   → convert to compact DER (~300 bytes)
7. tropicctl mem write 1 --in client.der  → store cert in TROPIC01 R-MEM slot 1
8. Write OpenVPN config         → pkcs11-providers + pkcs11-id directives
9. /etc/init.d/openvpn start   → OpenVPN authenticates via TROPIC01 on every handshake
```

---

## Security Properties Provided by TROPIC01

| Property | How TROPIC01 provides it |
|---|---|
| Private key non-extractability | Key generated and stored in tamper-resistant ECC slot; no export command exists |
| Hardware-bound identity | Chip serial embedded in PKCS#11 token label; cert stored on chip |
| True randomness | On-chip TRNG used during key generation |
| TLS signing | Raw ECDSA-P256 performed inside chip; only the signature is returned |
| Mutual TLS without software key | PKCS#11 bridge forwards all sign operations to hardware |

---

## Custom Patches to `libtropic-pkcs11`

These patches (in `features/core/libtropic-pkcs11/patches/`) adapt the upstream library for this
SPI-based deployment:

| Patch | Purpose |
|---|---|
| `001-use-linux-spi-hal` | Replace USB HAL with Linux SPI HAL |
| `002-use-spi-device-structure` | CMake-configurable SPI/GPIO device paths |
| `003-use-engineering-sample-keys` | Default slot-0 pairing key: ENGINEERING_SAMPLES_01 |
| `004-add-cko-certificate-from-r-mem` | Expose the certificate in R-MEM slot N+1 as a PKCS#11 `CKO_CERTIFICATE` object |
| `006-add-syslog-tracing` | Trace module calls to syslog |
| `007-load-per-unit-pairing-key` | Use the per-unit pairing key from `/etc/tropic01/pairing.*` when present |
| `008-port-to-libtropic-4.2` | Build against libtropic 4.2; sign the 32-byte hash with `lt_ecc_ecdsa_sign()` |
| `009-select-slot0-key-at-runtime` | Choose the slot-0 key (`es01` or `prod0`) from `$TROPICCTL_SH0` or `/etc/tropic01/slot0-key` |

---

## AWS IoT (not working, not claimed)

AWS IoT telemetry does not work yet and is not part of this feature's claim (backlog 3.1). It is
meant to use its own key in **ECC slot 1**, with its certificate in **R-MEM slot 2**; the VPN uses
ECC slot 0. The `aws/` directory holds only the Amazon root CAs. See
[aws-iot-known-issues.md](aws-iot-known-issues.md).

---

## Firmware Packages Required

| Package | Path | Purpose |
|---|---|---|
| `libtropic` | `features/core/libtropic` | TROPIC01 base SDK |
| `libtropic-pkcs11` | `features/core/libtropic-pkcs11` | PKCS#11 provider (SPI-patched) |
| `tropicctl` | `features/core/tropicctl` | Command-line tool (ECC keys, R-MEM, pairing keys, configuration); also installed as `lt-util` |
| `pkcs11-helper` | `features/core/pkcs11-helper` | OpenVPN PKCS#11 dependency |
| `openvpn-openssl` | `features/cra-03-key-storage/openvpn` | OpenVPN with PKCS#11 support |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `LT_L2_SPI_ERROR` | Bad SPI wiring or wrong device node | Check `/dev/spidev2.0`, CS GPIO pin 25 |
| `LT_HOST_SECURE_CHANNEL_SETUP` | Wrong pairing key | Set `es01` or `prod0` in `/etc/tropic01/slot0-key`; a provisioned unit needs `/etc/tropic01/pairing.*` |
| Anything on R-MEM slot 0 | Slot 0 is not used (backlog 3.14) | Use slot 1+ |
| `OpenSSL: error:bad signature` on server | Double-hashing | Ensure patch `008-port-to-libtropic-4.2` is applied |
| No cert found in `--show-pkcs11-ids` | Cert not stored or wrong slot | Re-run `tropicctl mem write 1 --in client.der`; it reads the slot back and says so |
| TLS handshake timeout (60 s) | Server-side `bad signature` | Patch 008 missing |
