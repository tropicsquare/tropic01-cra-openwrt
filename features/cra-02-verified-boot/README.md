# cra-02-verified-boot — firmware integrity and rollback protection

**CRA Annex I, Part I, 2(f) and 2(c). Status: not started.**

## At a glance

| | |
|---|---|
| Requirement | CRA Annex I, Part I, 2(f) and 2(c): integrity of firmware, no known vulnerabilities shipped |
| Status | **not started** (design outline only) |
| Hardware needed | OpenWrt One; possibly SoC eFuse programming (irreversible) |

## Role of TROPIC01

TROPIC01 is **not the boot root of trust.** The SoC's BootROM verifies the first boot stage
against keys in the SoC's own eFuses; the chip is never consulted at that point. Its possible
role is secondary: a **monotonic counter** to make rollback protection hardware-backed (the
installed firmware version can only go up), and a place to keep a signing or anchor value. Both
are design options, not implemented, and the chip's counters cannot be reset, so the design
needs care before anyone runs it on a real unit.

## Goal

The router only boots, and only installs via sysupgrade, firmware that is
signed by the vendor key, and never firmware *older* than what it already
accepted.

## Design outline

| Layer | Mechanism | Root of trust |
|---|---|---|
| BL2 → FIP (TF-A) → U-Boot | MediaTek secure boot | ROTPK hash fused into the MT7981 SoC |
| U-Boot → kernel/rootfs | FIT image signature check (`CONFIG_FIT_SIGNATURE`) | public key embedded in the verified U-Boot |
| sysupgrade | signature check in the fwtool/sysupgrade path before flashing | same key |
| anti-rollback | image carries a security version; install is refused if it is lower than a TROPIC01 monotonic counter, which is incremented after a successful upgrade | TROPIC01 monotonic counter |

**TROPIC01 is not the boot root of trust** — the SoC boot ROM is. Keep
docs and talk material consistent with that.

The OpenWrt One also has a SPI-NOR recovery flash with a hardware
write-protect switch. A signed recovery image in write-protected NOR is the
natural base for cra-01's *reset integrity*.

## Expected contents

- `openwrt-patches/` additions for U-Boot/TF-A (FIT signature, secure boot)
- `host-tools/` for key generation and image signing
- a small package with the sysupgrade-side check

## Done means

- [ ] unsigned or wrongly signed image is refused by U-Boot and by sysupgrade
- [ ] an older signed image is refused (counter check), a newer one accepted
- [ ] the full chain has been tested on hardware **with non-production keys**
- [ ] this README says what is still not covered
