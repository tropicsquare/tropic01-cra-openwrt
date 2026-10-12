# pkcs11-test - Lightweight PKCS#11 Testing Utility

A minimal testing tool for PKCS#11 modules on OpenWrt (67KB stripped binary).

## Description

This utility provides basic testing capabilities for PKCS#11 modules without the overhead of full tools like OpenSC's pkcs11-tool. It's specifically designed for embedded environments where space is limited.

## Features

- Load and initialize PKCS#11 modules
- Display module information (version, manufacturer, description)
- Verify basic PKCS#11 functionality
- Minimal dependencies (only libdl)
- Small footprint (67KB stripped)

## Building

Standard OpenWrt package build, from the repo root:

```bash
make package PKG=pkcs11-test
```

(Earlier versions of this package shipped a `manual-build.sh` workaround for
a build issue caused by compiling directly from `$(CURDIR)`. The sources
now live under `src/` and are copied into `$(PKG_BUILD_DIR)` like any other
package, so the standard package build works and the workaround is gone.)

Select it in the image build with `CONFIG_PACKAGE_pkcs11-test=y`, or build
and install it standalone on a running router:

```bash
scp build/openwrt/bin/packages/aarch64_cortex-a53/tropic01/pkcs11-test-*.apk root@<router-ip>:/tmp/
ssh root@<router-ip> 'apk add --allow-untrusted /tmp/pkcs11-test-*.apk'
```

## Usage

```bash
pkcs11-test --module /path/to/module.so [options]
```

`--module` is required. Options:

| Option | What it does |
|---|---|
| `--show-info` | Show module information (the default when no other option is given) |
| `--test-init` | Test initialization and finalization |
| `--list-objects` | List all objects in the token |
| `--random N` | Get N random bytes from the token (1-1024) |
| `--help`, `-h` | Show the help text |

Example:
```bash
pkcs11-test --module /usr/lib/libtropic_pkcs11.so
```

## Expected Output

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

## Files

- **src/pkcs11-test.c**: Source code (about 16 KB)
- **Makefile**: OpenWrt package definition

## Technical Details

### Architecture
- Target: aarch64 (ARM Cortex-A53)  
- Toolchain: gcc-14.3.0 with musl libc
- Format: ELF 64-bit LSB executable
- Interpreter: /lib/ld-musl-aarch64.so.1

### Compilation Flags
- `-Os`: Optimize for size
- `-pipe`: Use pipes instead of temporary files
- `-mcpu=cortex-a53`: Target CPU optimization
- `-ldl`: Link against libdl for dynamic loading

### Binary Size
- Unstripped: 73KB
- Stripped: 67KB

## Troubleshooting

### "error while loading shared libraries: libdl.so.2"
The musl libc on OpenWrt includes libdl functionality in libc itself. This error shouldn't occur with the cross-compiled binary.

### "cannot dynamically load executable"
Ensure the binary has execute permissions:
```bash
chmod +x /usr/bin/pkcs11-test
```

### Module loading fails
Check that the PKCS#11 module path is correct and the module is installed:
```bash
ls -lh /usr/lib/libtropic_pkcs11.so
file /usr/lib/libtropic_pkcs11.so
```

## License

GPL-2.0 (matching OpenWrt licensing)

## Author

Created for testing libtropic-pkcs11 on OpenWrt systems.
