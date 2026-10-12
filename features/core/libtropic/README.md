# Libtropic OpenWrt Package

This package provides the libtropic SDK library for OpenWrt, enabling application development with TROPIC01 secure elements.

## Overview

Libtropic is the official SDK for TROPIC01 secure element written in C. It provides:
- Secure communication protocols
- Cryptographic operations abstraction
- Hardware abstraction for multiple platforms
- Helper functions for common operations

## Package Details

- **Version**: 4.2.0
- **License**: BSD-3-Clause-Clear
- **Upstream**: https://github.com/tropicsquare/libtropic

## Installation

To include libtropic in your OpenWrt build:

```bash
make package PKG=libtropic
```

## Usage in Applications

### CMake Integration

Consumers build the pinned source tree, not a prebuilt library. The package
installs the libtropic source under `$(STAGING_DIR)/usr/src/libtropic`.
libtropic is configured by compile definitions that must match between the
library and its users, so each consumer adds the tree with
`add_subdirectory()` and picks its HAL and CAL from it. `tropicctl` and
`libtropic-pkcs11` do this, with the Linux SPI HAL and the OpenSSL CAL.

Pass the tree in from the package Makefile:

```makefile
CMAKE_OPTIONS += -DLIBTROPIC_SRC=$(STAGING_DIR)/usr/src/libtropic
```

Example CMakeLists.txt (as in `tropicctl/CMakeLists.txt`):

```cmake
find_package(OpenSSL REQUIRED)

add_subdirectory(${LIBTROPIC_SRC} libtropic)

# CAL: OpenSSL
add_subdirectory(${LIBTROPIC_SRC}/cal/openssl cal_openssl)
target_sources(tropic PRIVATE ${LT_CAL_SRCS})
target_include_directories(tropic PUBLIC ${LT_CAL_INC_DIRS})
target_link_libraries(tropic PUBLIC OpenSSL::Crypto)

# HAL: Linux SPI
add_subdirectory(${LIBTROPIC_SRC}/hal/linux/spi linux_spi_hal)
target_sources(tropic PRIVATE ${LT_HAL_SRCS})
target_include_directories(tropic PUBLIC ${LT_HAL_INC_DIRS})

add_executable(myapp main.c)
target_link_libraries(myapp PRIVATE tropic OpenSSL::Crypto)
```

## Available HAL (Hardware Abstraction Layers)

The source tree includes several HAL implementations, among them:

- **Linux SPI**: `usr/src/libtropic/hal/linux/spi/`
  - For hardware SPI communication on Linux (used here)
- **POSIX TCP**: `usr/src/libtropic/hal/posix/tcp/`
  - For testing with the TROPIC01 model over TCP
- **POSIX USB devkit**: `usr/src/libtropic/hal/posix/usb_devkit/`
  - For the USB devkit

Others in the tree: `hal/linux/spi_native_cs/`, `hal/mock/` and HALs for
microcontroller platforms. Choose the one for your hardware setup.

## Available CAL (Crypto Abstraction Layers)

- **OpenSSL**: `usr/src/libtropic/cal/openssl/`
  - Used here (the package depends on `libopenssl`)

`cal/mbedtls_v4/`, `cal/trezor_crypto/` and `cal/wolfcrypt/` are also in the
tree.

## Configuration Options

Libtropic supports several build-time configuration options. For OpenWrt, the defaults are:

- `LT_HELPERS=ON` - Helper functions enabled
- `LT_LOG_LVL=None` - Logging disabled for production
- `LT_BUILD_EXAMPLES=OFF` - Examples not built
- `LT_BUILD_TESTS=OFF` - Tests not built

If you need different settings, modify the package Makefile's CMAKE_OPTIONS.

## Documentation

Full documentation is available at:
- https://tropicsquare.github.io/libtropic/latest/
- https://github.com/tropicsquare/libtropic

## Compatibility

libtropic 4.2.0 is compatible with (from its upstream README):
- TROPIC01 Bootloader FW: 1.0.1–2.0.1
- TROPIC01 Application FW: 1.0.0–2.2.0
- TROPIC01 SPECT FW: 1.0.0–1.3.0

## Support

For issues related to:
- **OpenWrt packaging**: Open issue in your OpenWrt repository
- **Libtropic library**: https://github.com/tropicsquare/libtropic/issues
- **TROPIC01 chip**: https://github.com/tropicsquare/tropic01

## License

This package follows OpenWrt licensing (GPL-2.0).
Libtropic library is licensed under BSD-3-Clause-Clear license.
