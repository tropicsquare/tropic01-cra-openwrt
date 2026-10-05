# Libtropic OpenWrt Package

This package provides the libtropic SDK library for OpenWrt, enabling application development with TROPIC01 secure elements.

## Overview

Libtropic is the official SDK for TROPIC01 secure element written in C. It provides:
- Secure communication protocols
- Cryptographic operations abstraction
- Hardware abstraction for multiple platforms
- Helper functions for common operations

## Package Details

- **Version**: 3.0.0
- **License**: BSD-3-Clause-Clear
- **Upstream**: https://github.com/tropicsquare/libtropic

## Installation

To include libtropic in your OpenWrt build:

```bash
make menuconfig
# Navigate to: Libraries -> libtropic
# Select <*> to include in build
make package/libtropic/compile V=s
```

## Usage in Applications

### CMake Integration

Libtropic is built as a static library without HAL/CAL. Your application must:

1. Link against libtropic
2. Provide HAL (Hardware Abstraction Layer) implementation
3. Provide CAL (Crypto Abstraction Layer) implementation

Example CMakeLists.txt:

```cmake
# Add libtropic include directory
include_directories(/usr/include/libtropic)

# Add your application sources
add_executable(myapp main.c)

# Add CAL for MbedTLS v4
set(CAL_SRCS
    /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_common.c
    /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_aesgcm.c
    /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_sha256.c
    /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_hmac_sha256.c
    /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_x25519.c
)

# Add HAL for Linux SPI
set(HAL_SRCS
    /usr/include/libtropic/hal/linux/spi/libtropic_port_linux_spi.c
)

target_sources(myapp PRIVATE ${CAL_SRCS} ${HAL_SRCS})

# Link libraries
target_link_libraries(myapp
    /usr/lib/libtropic.a
    mbedtls
    mbedcrypto
)
```

### Makefile Integration

```makefile
CFLAGS += -I/usr/include/libtropic
LDFLAGS += -L/usr/lib
LDLIBS += -ltropic -lmbedtls -lmbedcrypto

SRCS = main.c \
       /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_common.c \
       /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_aesgcm.c \
       /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_sha256.c \
       /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_hmac_sha256.c \
       /usr/include/libtropic/cal/mbedtls_v4/lt_mbedtls_v4_x25519.c \
       /usr/include/libtropic/hal/linux/spi/libtropic_port_linux_spi.c

myapp: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) $(LDLIBS) -o myapp
```

## Available HAL (Hardware Abstraction Layers)

The package includes source files for multiple HAL implementations:

- **Linux SPI**: `/usr/include/libtropic/hal/linux/spi/`
  - For hardware SPI communication on Linux
- **POSIX TCP**: `/usr/include/libtropic/hal/posix/tcp/`
  - For testing with TROPIC01 model over TCP
- **POSIX USB Dongle**: `/usr/include/libtropic/hal/posix/usb_dongle/`
  - For USB dongle communication

Choose the appropriate HAL for your hardware setup.

## Available CAL (Crypto Abstraction Layers)

The package includes CAL implementation for:

- **MbedTLS v4**: `/usr/include/libtropic/cal/mbedtls_v4/`
  - Requires MbedTLS library (automatically included as dependency)

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

This package is compatible with:
- TROPIC01 Bootloader FW: 1.0.0–2.0.0
- TROPIC01 Application FW: 1.0.0
- TROPIC01 SPECT FW: 2.0.1

## Support

For issues related to:
- **OpenWrt packaging**: Open issue in your OpenWrt repository
- **Libtropic library**: https://github.com/tropicsquare/libtropic/issues
- **TROPIC01 chip**: https://github.com/tropicsquare/tropic01

## License

This package follows OpenWrt licensing (GPL-2.0).
Libtropic library is licensed under BSD-3-Clause-Clear license.
