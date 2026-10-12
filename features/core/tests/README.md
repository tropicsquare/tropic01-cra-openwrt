# Ad-hoc PKCS#11 test files

These are router-side diagnostics written while bringing up
`libtropic-pkcs11`. They are **not built by this feed** — none of them
have a package Makefile — and are kept only as reference/copy-paste
material for manual debugging over SSH.

For an actual buildable, installable test tool, use the `pkcs11-test`
package (`make package PKG=pkcs11-test`) instead.

| File | Purpose |
|---|---|
| `test-pkcs11-full.c` | PKCS#11 smoke test: init, slot list, session, random |
| `test-pkcs11-info.c` | Module info query (Cryptoki version, manufacturer, …) |
| `test-slot-list.c` | Slot enumeration |
| `pkcs11-data.c` | R-MEM/ECC data operations reference (see `../docs/pkcs11-data-operations.md`) |
| `quick-test.sh` | Quick smoke test, expects a cross-compiled `test-pkcs11-full` at `/tmp/` on the router |
| `test-pkcs11-comprehensive.sh` | Same idea, more verbose output |

To use one of the `.c` files: cross-compile it by hand with the OpenWrt
toolchain in `build/openwrt/staging_dir/toolchain-*/bin/`, copy the binary
to the router, and run it over SSH. There is no `make` target for these —
that is the point: they are throwaway diagnostics, not packaged software.
