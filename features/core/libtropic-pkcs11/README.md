# Libtropic-PKCS11 OpenWrt Package

This package provides the libtropic-pkcs11 PKCS#11 module for OpenWrt, enabling standard cryptographic token interface access to TROPIC01 secure element features.

## Overview

Libtropic-pkcs11 is a PKCS#11 v2.40 Cryptoki module that exposes TROPIC01's cryptographic capabilities through the industry-standard PKCS#11 API. This enables using TROPIC01 as a hardware security module (HSM) with any PKCS#11-compatible software.

## Features

- **Hardware Random Number Generation** - True random numbers from TROPIC01's TRNG
- **Secure Data Storage** - 512 slots of R-MEM storage (up to 444 bytes each)
- **ECC Key Management** - Generate and use P-256 (secp256r1) and Ed25519 keys
- **Digital Signatures** - ECDSA and EdDSA signing (private keys never leave chip)
- **Standard Interface** - Compatible with pkcs11-tool, OpenSSL, Firefox, etc.

## Build Configuration

- **Upstream**: https://github.com/tropicsquare/libtropic-pkcs11
- **Version**: 0.1.0
- **License**: BSD-3-Clause-Clear

## Building

To include libtropic-pkcs11 in your OpenWrt build:

```bash
make menuconfig
# Navigate to: Libraries -> libtropic-pkcs11

make package/libtropic-pkcs11/compile V=s
```

## Installation

The package installs the PKCS#11 module to `/usr/lib/libtropic_pkcs11.so`

## Usage Examples

**Note**: The following examples require `pkcs11-tool` from the `opensc` package.
To install it, enable **Utilities → opensc** in menuconfig.

### Test Module
```bash
# First, install opensc package for pkcs11-tool:
# opkg install opensc

pkcs11-tool --module /usr/lib/libtropic_pkcs11.so --show-info
```

### Generate Random Numbers
```bash
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so \
    --generate-random 32 --output-file /tmp/random.bin
```

### Generate ECC Key Pair
```bash
# Generate P-256 key in slot 24 (0x18 hex)
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so \
    --keypairgen --key-type EC:secp256r1 --id "18"
```

### Sign Data
```bash
# Create test data (32 bytes for ECDSA)
echo "0123456789ABCDEF0123456789ABCDEF" > /tmp/hash.bin

# Sign with key in slot 24
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so \
    --sign --mechanism ECDSA --id "18" \
    --input-file /tmp/hash.bin --output-file /tmp/sig.bin
```

### Store User Data
```bash
# Write data to R-MEM slot 60
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so \
    --write-object /tmp/data.bin --type data --label "60"

# Read data back
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so \
    --read-object --type data --label "60" -o /tmp/read.bin
```

## Configuration

The module is compiled with these defaults (set via CMake options in the Makefile):

| CMake variable | Default | Description |
|---|---|---|
| `TS_SPI_DEV` | `/dev/spidev2.0` | SPI bus device node |
| `TS_GPIO_DEV` | `/dev/gpiochip0` | GPIO chip for chip-select |
| `TS_GPIO_CS_PIN` | `25` | GPIO pin number used as SPI CS |
| `TS_SPI_SPEED` | `1000000` | SPI clock frequency in Hz (1 MHz) |
| `LT_SH0_KEYS` | `eng_sample` | Pairing key set (`eng_sample` for ENGINEERING_SAMPLES_01) |
| `LT_PKCS11_LOG_EN` | `ON` | Enable PKCS#11 logging |

To change any of these, edit the `CMAKE_OPTIONS` block in the package `Makefile`.

## Dependencies

This package requires:
- `libtropic` - TROPIC01 SDK library
- `libopenssl` - OpenSSL cryptographic library

The libtropic package must be installed and configured before libtropic-pkcs11 can function.

### Optional Tools

For testing and development:
- `opensc` - Provides `pkcs11-tool` utility for testing PKCS#11 modules
  - Enable in menuconfig: **Utilities → opensc**
  - Or install: `opkg install opensc`

## Hardware Requirements

- TROPIC01-ES secure element (e.g. **Mikroe Click** breakout board)
- Connected via **SPI** to the host SoC:

  | Signal | Default mapping |
  |---|---|
  | SPI bus | `/dev/spidev2.0` |
  | Chip-select (GPIO) | `/dev/gpiochip0` pin **25** |
  | SPI clock | 1 MHz |

- Tested on: MediaTek MT7981B, OpenWrt One (aarch64 cortex-a53, `mediatek/filogic` target)
- The SPI bus and GPIO must be enabled in the device tree / board configuration

> **Note:** The default build uses `ENGINEERING_SAMPLES_01` pairing keys (`LT_SH0_KEYS=eng_sample`). This is required for ES01 silicon. Production chips require different keys.

## OpenSSL Integration

To use with OpenSSL, you need a PKCS#11 provider like `libp11`. Configuration example:

```ini
[openssl_init]
providers = provider_sect

[provider_sect]
pkcs11 = pkcs11_sect

[pkcs11_sect]
module = /usr/lib/libtropic_pkcs11.so
```

## Security Note

⚠️ The default pairing keys in libtropic are for development only. For production deployments, provision custom pairing keys in the TROPIC01 chip and update the keys in libtropic package.

## Troubleshooting

### SPI device not found
Verify the SPI bus is enabled and the device node exists:
```bash
ls -la /dev/spidev2.0
```
If missing, check that `spidev` is enabled in your kernel config and that the device tree assigns the SPI bus correctly.

### Permission denied on `/dev/spidev2.0` or `/dev/gpiochip0`
```bash
# OpenWrt root shell has access by default.
# If running as a non-root user:
chmod 666 /dev/spidev2.0
chmod 666 /dev/gpiochip0
```

### `LT_L2_SPI_ERROR` — SPI communication failure
1. Check wiring: MOSI, MISO, SCLK, CS all connected
2. Confirm CS GPIO pin matches `TS_GPIO_CS_PIN` (default: 25)
3. Lower SPI speed: set `TS_SPI_SPEED=500000` in the Makefile and rebuild
4. Verify `/dev/spidev2.0` maps to the correct SPI bus in the device tree

### `LT_HOST_SECURE_CHANNEL_SETUP` — Pairing key mismatch
The firmware was not built with the correct pairing keys for this chip batch.
For **ENGINEERING_SAMPLES_01** silicon, ensure `LT_SH0_KEYS=eng_sample` is set
(this is the default in the Makefile).

### Module Not Found
Verify installation:
```bash
ls -la /usr/lib/libtropic_pkcs11.so
```

### `LT_L3_R_MEM_DATA_WRITE_WRITE_FAIL` on slot 0
R-MEM slot 0 is read-protected on ENGINEERING_SAMPLES_01. Use slot 1 or higher.

## Documentation

For detailed PKCS#11 function reference and examples:
- See upstream repository: https://github.com/tropicsquare/libtropic-pkcs11
- PKCS#11 specification: OASIS PKCS#11 v2.40

## License

BSD-3-Clause-Clear - See LICENSE.md in the upstream repository
