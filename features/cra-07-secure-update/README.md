# cra-07-secure-update — signed updates and rollback protection

**CRA Annex I, Part I, 2(c) and 2(f), for the update path. Status: in progress: works end to end on a router with a spare chip (signed images accepted, unsigned, tampered, foreign and rolled-back images refused and not forceable, floor raised after boot); open items in the backlog, section 10.**

## At a glance

| | |
|---|---|
| Requirement | 2(c): security updates; 2(f): integrity of the software, here at the moment it is installed |
| Status | in progress: router side, build signing and host tool done; verified on a router with a spare chip (hardware report, 2026-10-11) |
| Irreversible step | initialising the rollback counter fixes its range; burning the counter-init right away (optional) |
| Not this feature | secure boot (the SoC and U-Boot checking what they boot): [cra-02-verified-boot](../cra-02-verified-boot/) |

## Role of TROPIC01

**Central for storage and state, not for the cryptography.** The chip holds the update **root public keys** (R-MEM slots
200 and 201) and the **rollback floor** (a monotonic counter). It does not check signatures: it has no verify command.
`ucert` on the router checks the image's signature chain against the root keys read from the chip.

## What it does

- `sysupgrade` (and LuCI's flash page, which uses the same validation) accepts an image only if
  1. it carries a `ucert` signature chain (OpenWrt's own format, made by `append-metadata` at build time),
  2. the chain leads to a root public key held in TROPIC01,
  3. its signed metadata carries a security version `tropic01_sv` that is not below this unit's floor.
- A refused image is marked **broken**: `sysupgrade -F` cannot install it. (OpenWrt's own check only marks it invalid, which
  `-F` overrides, and accepts unsigned images when `ucert` is missing.)
- At boot (S86, before OpenVPN) the floor is raised to the running image's security version. Only an image that booted
  raises it; booting an older image never lowers it.
- With the chip unreachable or the counter not initialised, nothing is accepted (fail closed).

Commands: `tropic01-update status | check IMAGE | commit | keys install SLOT FILE --yes-write-update-key | floor init
--yes-init-rollback-counter`. Design and reasoning: [docs/design.md](docs/design.md).

## What this does not fix

- **It is not secure boot.** Anyone who writes the flash directly (serial console, U-Boot, a flash programmer) skips
  `sysupgrade` and this check. That is cra-02.
- **Root on a running router** can edit these scripts, and while the device's pairing key may write R-MEM it can replace
  the root key in the chip. R-MEM rights are all-or-nothing per pairing key, so the vault's write right covers these slots too.
- **The floor can be lowered** by anyone holding a pairing key with the counter-init right (`counter init` can be repeated on
  the chip). The least-privilege profile (design, section 7) removes that right from the runtime key; it was run on the bench
  chip, but nothing provisions it automatically yet, and the runtime key keeps R-MEM write, so it can still replace the root
  key slots.
- **The security version is whatever the build says.** Raising it for every security fix is a release-process rule, not
  something this code can enforce.
- **Fail closed** means a unit with a dead or alarmed chip cannot be updated remotely.
- Certificate validity windows and a wrong clock before NTP can refuse good images: see the design, section 4.
- Only the spare chip has an update root key and an initialised counter, and the package is not in the default image
  (`openwrt.config` leaves it commented out).

## Building a signed image

1. Once, offline: `host-tools/tropic01-update-keys.sh new-root /secure/update-root` (the root key; `root.pub` goes into each
   unit's TROPIC01, `root.sec` never leaves the operator).
2. Per release key: `host-tools/tropic01-update-keys.sh issue /secure/update-root /secure/signing` (`key-build`, its certificate
   from the root, and a revoker certificate).
3. Per build: `make build TROPIC01_SIGNING_DIR=/secure/signing TROPIC01_IMAGE_SV=<n>`. OpenWrt's `append-metadata` signs the
   image; patch `0003` puts `"tropic01_sv": n` into the signed metadata and `/usr/share/tropic01/image-sv` into the root
   filesystem. Raise `n` only when a release must not be rolled back past.

Both directories must be outside any git work tree (the tools refuse otherwise).

## Tests

`tests/sandbox/secure-update.sh` (71 checks, mutation-checked; includes keys from the host tool and an image signed exactly as
`append-metadata` does) runs the tool with OpenWrt's `usign`, `ucert` and `fwtool`
and the real `tropicctl` on its fake chip. It needs those host tools from an OpenWrt build tree (`OPENWRT_DIR`) and skips
without them.
