# Testing libtropic-pkcs11

This guide provides instructions for testing the PKCS#11 module after installation.

## Quick Test Tool (Recommended)

Build it with `make package PKG=pkcs11-test`; the binary (67KB) lands at:
```
build/openwrt/bin/targets/mediatek/filogic/packages/pkcs11-test
```

### Using pkcs11-test

1. **Copy to your device:**
   ```bash
   scp build/openwrt/bin/targets/mediatek/filogic/packages/pkcs11-test root@<router-ip>:/usr/bin/
   ```

2. **Make executable:**
   ```bash
   ssh root@<router-ip> 'chmod +x /usr/bin/pkcs11-test'
   ```

3. **Test the module:**
   ```bash
   ssh root@<router-ip> 'pkcs11-test /usr/lib/libtropic_pkcs11.so'
   ```

### Expected Output

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

## Alternative: Using OpenSSL

OpenSSL (if available) can also test PKCS#11 modules:

```bash
# Load the module
openssl engine pkcs11 -t \
  -pre SO_PATH:/usr/lib/engines/pkcs11.so \
  -pre MODULE_PATH:/usr/lib/libtropic_pkcs11.so

# List available keys
openssl engine pkcs11 -post "PIN:1234"
```

## Rebuilding pkcs11-test

If you need to rebuild the test utility:
```bash
make package PKG=pkcs11-test
```

- **Source**: `features/core/pkcs11-test/src/pkcs11-test.c`
- **Binary**: `build/openwrt/bin/targets/mediatek/filogic/packages/pkcs11-test` (67KB stripped)
- **Architecture**: aarch64 (ARM64) for Cortex-A53

## Troubleshooting

### Module Not Found
```bash
# Check if module exists
ls -lh /usr/lib/libtropic_pkcs11.so

# Verify it's a valid ELF binary
file /usr/lib/libtropic_pkcs11.so
```

### USB Device Not Found
```bash
# Check USB device
ls -l /dev/ttyACM0

# View kernel messages
dmesg | tail -20
```

### Permissions Issues
```bash
# Ensure binary is executable
chmod +x /usr/bin/pkcs11-test

# Run with verbose output
pkcs11-test /usr/lib/libtropic_pkcs11.so
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
