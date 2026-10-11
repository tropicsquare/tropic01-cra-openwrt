# Integration Guide for libtropic-pkcs11

This guide explains how to integrate the libtropic-pkcs11 package into your OpenWrt firmware image.

## Quick Integration

### Method 1: Using menuconfig (Recommended)

1. Enter the OpenWrt build configuration menu:
   ```bash
   cd /path/to/tropic01-cra-openwrt
   make menuconfig
   ```

2. Navigate to: **Libraries** → **libtropic-pkcs11**

3. Enable the package by pressing `Y` or `M`:
   - `Y` = Built-in (included in firmware image)
   - `M` = Module (built as separate .apk package)

4. Save and exit (press `Esc` multiple times, then select "Yes" to save)

5. Build the firmware:
   ```bash
   make -j$(nproc)
   ```

### Method 2: Add to .config directly

Add the following line to your `.config` file:
```
CONFIG_PACKAGE_libtropic-pkcs11=y
```

Then run:
```bash
make defconfig
make -j$(nproc)
```

### Method 3: Add to Default Packages (Target-Specific)

To include libtropic-pkcs11 by default for specific target, edit the target's Makefile:

Example for MediaTek:
```bash
# Edit: target/linux/mediatek/Makefile
DEFAULT_PACKAGES += libtropic-pkcs11
```

## Building Only the Package

To build just the libtropic-pkcs11 package without building the entire firmware:

```bash
# Clean previous build
make -C build/openwrt package/feeds/tropic01/libtropic-pkcs11/clean V=s

# Download sources
make -C build/openwrt package/feeds/tropic01/libtropic-pkcs11/download V=s

# Compile the package
make package PKG=libtropic-pkcs11
```

The compiled library will be available at:
```
build_dir/target-<arch>/libtropic-pkcs11-0.1.0/libtropic_pkcs11.so
```

## Verification

After building, verify the package was created:

```bash
# Find the .apk package (after full build)
find ./bin -name "*libtropic-pkcs11*.apk"
```

To test on a running OpenWrt device:

```bash
# Copy the .so file to the device
scp build_dir/target-*/libtropic-pkcs11-*/libtropic_pkcs11.so root@<device-ip>:/usr/lib/

# Test the module (pkcs11-test package)
pkcs11-test --module /usr/lib/libtropic_pkcs11.so --show-info
```

## Dependencies

The following packages will be automatically selected as dependencies:
- `libtropic` - TROPIC01 SDK library
- `libopenssl` - OpenSSL cryptographic library

Ensure these are available and properly configured before building libtropic-pkcs11.

## Installation on Device

Once the firmware is flashed or the package is installed, the PKCS#11 module will be available at:
```
/usr/lib/libtropic_pkcs11.so
```

Use it with any PKCS#11-compatible software:

```bash
# With pkcs11-test
pkcs11-test --module /usr/lib/libtropic_pkcs11.so --list-objects

# With OpenSSL 3 (pkcs11-provider, as cra-03-key-storage configures it)
OPENSSL_CONF=/etc/ssl/tropic01-aws/openssl-aws.cnf \
  openssl pkeyutl -sign -inkey "pkcs11:id=%01;type=private" -in hash.bin -out sig.bin
```

## Hardware Setup

TROPIC01 is connected via SPI:
- SPI bus: `/dev/spidev2.0`, 1 MHz
- Chip-select: `/dev/gpiochip0` pin 25
- Check with: `ls -l /dev/spidev2.0 /dev/gpiochip0`

These are set in the package `Makefile` as `TS_SPI_DEV`, `TS_GPIO_DEV`,
`TS_GPIO_CS_PIN` and `TS_SPI_SPEED`.

## Troubleshooting

### Package not visible in menuconfig
- Run `make menuconfig` again
- Check that all dependencies are satisfied
- Verify the Makefile syntax in `features/core/libtropic-pkcs11/Makefile`

### Build fails with dependency errors
- Ensure libtropic package is available and compiles successfully:
  ```bash
  make package PKG=libtropic
  ```
- Check OpenSSL is configured:
  ```bash
  make -C build/openwrt package/libopenssl/compile V=s
  ```

### Module loads but can't communicate with TROPIC01
- Check the SPI wiring and `/dev/spidev2.0`
- Check pairing keys match your chip batch (see libtropic documentation)
- Test with `tropicctl session test` first to ensure basic connectivity

## Advanced Configuration

### Custom SPI device

Edit `features/core/libtropic-pkcs11/Makefile` and modify:
```makefile
CMAKE_OPTIONS += \
    -DTS_SPI_DEV="/dev/spidevX.Y"
```

### Logging

Logging is on by default (`LT_PKCS11_LOG_EN=ON` in the Makefile). Messages go
to syslog; read them with `logread`. To turn it off, set
`-DLT_PKCS11_LOG_EN=OFF` and rebuild:
```bash
make -C build/openwrt package/feeds/tropic01/libtropic-pkcs11/clean
make package PKG=libtropic-pkcs11
```

## Package Structure

```
features/core/libtropic-pkcs11/
├── Config.in          # Package configuration for menuconfig
├── INTEGRATION.md     # This guide
├── Makefile           # Build instructions
├── README.md          # Package documentation
├── TESTING.md         # Testing on the router
└── patches/           # 001-004, 006-009, applied to the upstream source
```

## Further Information

- Upstream repository: https://github.com/tropicsquare/libtropic-pkcs11
- libtropic documentation: https://github.com/tropicsquare/libtropic
- PKCS#11 specification: OASIS PKCS#11 v2.40
- OpenWrt package development: https://openwrt.org/docs/guide-developer/packages

## License

BSD-3-Clause-Clear - See LICENSE.md in upstream repository
