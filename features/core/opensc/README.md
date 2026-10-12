# OpenSC Package for OpenWrt

This package provides OpenSC tools for testing and working with PKCS#11 modules and smart cards.

## Overview

OpenSC is an open-source project that provides a set of libraries and utilities for working with smart cards. This is a **minimal build** optimized for OpenWrt, specifically for testing PKCS#11 modules like libtropic-pkcs11.

> **Status:** currently fails to build against OpenWrt's fortify headers
> (`__orig_strlcpy is not a function`). It is not selected in
> `openwrt.config`. Use [pkcs11-test](../pkcs11-test/README.md) instead.

## What's Included

### Tools
- **pkcs11-tool** - Test and debug PKCS#11 modules (main utility)
- **opensc-tool** - Query smart card information
- **pkcs15-tool** - Manage PKCS#15 data structures
- **opensc-asn1** - ASN.1 encoding/decoding utility

### Libraries
- **libopensc** - Core OpenSC library for PKCS#11 operations

## Special Build Configuration

This package is built **without PC/SC middleware dependencies** to reduce size and complexity. This is perfect for:
- Testing external PKCS#11 modules (like libtropic-pkcs11)
- Embedded systems with limited resources
- PKCS#11 modules for directly attached chips (TROPIC01 is on SPI)

## Building

To include in your OpenWrt build:

```bash
make package PKG=opensc
```

## Usage with libtropic-pkcs11

### Test Module Info
```bash
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so --show-info
```

### List Slots
```bash
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so --list-slots
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

### List Objects
```bash
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so \
    --list-objects
```

### Sign Data
```bash
# Create test data
echo -n "0123456789ABCDEF0123456789ABCDEF" > /tmp/hash.bin

# Sign with ECDSA
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so \
    --sign --mechanism ECDSA --id "18" \
    --input-file /tmp/hash.bin --output-file /tmp/sig.bin
```

### Store User Data in R-MEM
```bash
# Write data to slot 60
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so \
    --write-object /tmp/data.bin --type data --label "60"

# Read data back
pkcs11-tool --module /usr/lib/libtropic_pkcs11.so \
    --read-object --type data --label "60" -o /tmp/read.bin
```

## Installation

### During Build
The package will be included in your firmware if enabled in menuconfig.

## Package Size

Approximate installed size: ~600KB (minimal build without PC/SC)

## Dependencies

- libopenssl (for cryptographic operations)
- zlib (for compression)

## Compatibility

- Works with any PKCS#11 v2.x module
- No smart card reader required (when using external modules)

## Source

- **Upstream**: https://github.com/OpenSC/OpenSC
- **Version**: 0.26.0
- **License**: LGPL-2.1-or-later

## Troubleshooting

### Module not found
Ensure the module path is correct:
```bash
ls -la /usr/lib/libtropic_pkcs11.so
```

### No slots found
This is normal when testing external PKCS#11 modules that don't use PC/SC.
The module is still functional for its specific hardware (like TROPIC01).

### Device permission denied
```bash
chmod 666 /dev/spidev2.0
chmod 666 /dev/gpiochip0
```

## Alternative: Manual Testing

If you prefer not to install opensc, you can test PKCS#11 modules using OpenSSL:

```bash
# List available providers
openssl list -providers
```

## Support

For OpenSC issues: https://github.com/OpenSC/OpenSC/issues
For libtropic-pkcs11: https://github.com/tropicsquare/libtropic-pkcs11
