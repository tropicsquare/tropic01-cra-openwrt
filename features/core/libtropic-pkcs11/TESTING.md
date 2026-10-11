# Testing libtropic-pkcs11

This guide provides instructions for testing the PKCS#11 module after installation.

## Quick Test Tool (Recommended)

Build it with `make package PKG=pkcs11-test`; the package lands at:
```
build/openwrt/bin/packages/aarch64_cortex-a53/tropic01/pkcs11-test-*.apk
```

### Using pkcs11-test

1. **Copy to your device:**
   ```bash
   scp build/openwrt/bin/packages/aarch64_cortex-a53/tropic01/pkcs11-test-*.apk root@<router-ip>:/tmp/
   ```

2. **Install it:**
   ```bash
   ssh root@<router-ip> 'apk add --allow-untrusted /tmp/pkcs11-test-*.apk'
   ```

3. **Test the module:**
   ```bash
   ssh root@<router-ip> 'pkcs11-test --module /usr/lib/libtropic_pkcs11.so'
   ```

### Expected Output

```
Loading PKCS#11 module: /usr/lib/libtropic_pkcs11.so
✓ Module loaded successfully
✓ C_GetFunctionList: OK
  Cryptoki version: 2.40

Initializing module...
✓ C_Initialize: OK

=== Module Information ===
Cryptoki version: 2.40
Manufacturer ID:  TropicSquare
Library desc:     Tropic PKCS11
Library version:  0.0
Flags:            0x00000000

Finalizing module...
✓ C_Finalize: OK

=== Test Summary ===
Module:     /usr/lib/libtropic_pkcs11.so
Status:     PASSED
The PKCS#11 module appears to be functional.
```

`Status: PASSED` is printed whenever the tool reaches the end; check the `✓`
lines and look for `Warning:` or `Error:` lines on stderr.

## Alternative: Using OpenSSL

OpenSSL 3 with `pkcs11-provider`, as `cra-03-key-storage` configures it, can sign with a key in the chip:

```bash
OPENSSL_CONF=/etc/ssl/tropic01-aws/openssl-aws.cnf \
  openssl pkeyutl -sign -inkey "pkcs11:id=%01;type=private" -in hash.bin -out sig.bin
```

## Rebuilding pkcs11-test

If you need to rebuild the test utility:
```bash
make package PKG=pkcs11-test
```

- **Source**: `features/core/pkcs11-test/src/pkcs11-test.c`
- **Package**: `build/openwrt/bin/packages/aarch64_cortex-a53/tropic01/pkcs11-test-*.apk`
- **Architecture**: aarch64 (ARM64) for Cortex-A53

## Troubleshooting

### Module Not Found
```bash
# Check if module exists
ls -lh /usr/lib/libtropic_pkcs11.so

# Verify it's a valid ELF binary
file /usr/lib/libtropic_pkcs11.so
```

### SPI Device Not Found
```bash
# Check the SPI bus and the chip-select GPIO
ls -l /dev/spidev2.0 /dev/gpiochip0

# View kernel messages
dmesg | tail -20
```

### Permissions Issues
```bash
# Ensure binary is executable
chmod +x /usr/bin/pkcs11-test

# Run it
pkcs11-test --module /usr/lib/libtropic_pkcs11.so
```

## Advanced Testing with p11-kit (Optional)

If `p11-kit` is available:
```bash
# List modules
p11-kit list-modules

# Show module information
p11-kit list-objects "pkcs11:model=PKCS%2315%20emulated"
```

## Next Steps

After successful testing:
1. **Configure Applications**: Update apps to use the PKCS#11 module
2. **Key Management**: Use the module for cryptographic operations
3. **Integration**: Integrate with OpenVPN, OpenSSH, or other services

## Build Information

- **Test Utility**: pkcs11-test v1.0.0 (source: `features/core/pkcs11-test/src/`)
- **Compilation**: Standard OpenWrt package build, `aarch64-openwrt-linux-musl-gcc` toolchain
- **Dependencies**: Only `libdl` (dynamic loading library)
- **Size**: 67KB (stripped)

## Additional Resources

- PKCS#11 Specification: http://docs.oasis-open.org/pkcs11/pkcs11-base/v2.40/
- libtropic GitHub: https://github.com/tropicsquare/libtropic
- libtropic-pkcs11 GitHub: https://github.com/tropicsquare/libtropic-pkcs11
