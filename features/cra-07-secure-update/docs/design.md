# Secure update: design

Signed `sysupgrade` images, a trust root kept in TROPIC01, and rollback protection with a TROPIC01 monotonic counter.
Secure boot (the SoC and U-Boot checking what they boot) is a separate feature, `cra-02-verified-boot`, not this one.

Statements are marked *measured* (run in this project), *from the source* (the pinned OpenWrt tree), or *to verify*.

## 1. What OpenWrt does today on this board

- The build can sign an image: `append-metadata` appends JSON metadata with `fwtool`, signs the image with `usign` and
  attaches a `ucert` certificate chain, **if** `key-build` and `key-build.ucert` exist in the build tree. They do not, so
  images are unsigned. *From the source.*
- On the router, `validate_firmware_image` calls `fwtool_check_signature`, which returns success when `ucert` is not
  installed (it is not in the image) or the image has no signature, unless `REQUIRE_IMAGE_SIGNATURE=1`. A failure only
  marks the image *invalid*, which `sysupgrade -F` overrides. *From the source.*
- `platform_check_image` on the OpenWrt One runs `fit_check_sign -f IMAGE` without a key: it checks the FIT hashes, not who
  made the image. *From the source.*

So today any structurally valid image is accepted, and the signature check, where it exists, can be forced past.

## 2. The scheme

```
 offline (operator)                        build                               router
 ------------------                        -----                               ------
 root key (usign)  ──issues──► release key certificate (ucert -I)
                                 key-build + key-build.ucert ──► append-metadata signs every image
                                 security version SV ──► "tropic01_sv" in the signed metadata
 root PUBLIC key ─────────────────────────────────────────────────────────► TROPIC01 R-MEM slot 200 (201 spare)
                                                                             rollback floor ─► TROPIC01 counter
```

Acceptance (in `validate_firmware_image`, before anything is written):

1. the image carries a signature and certificate chain (`fwtool -s`), else refuse;
2. the root public keys are read from TROPIC01 R-MEM into a temporary key directory;
3. `ucert -V` checks the chain from that root to the release key and the release key's signature over the image;
4. the signed metadata carries `tropic01_sv`, and `SV >= floor`, where `floor = START - counter`;
5. the accepted SV is recorded as *pending* in `/etc/tropic01/` (kept across the upgrade).

A refusal marks the image **broken**, which `sysupgrade -F` cannot override.

Commit (at boot, after the new image runs): if the pending record matches the running image (`DISTRIB_REVISION`), the
counter is decremented until `floor = SV`. A failed upgrade never raises the floor; only a booted image does.

## 3. Why the root key in TROPIC01

| Property | Root public key in `/etc/opkg/keys` (flash) | Root public key in TROPIC01 R-MEM |
|---|---|---|
| A factory reset or a reflash replaces it | yes | no: it stays with the chip |
| A settings backup put back can replace it | yes | no |
| Someone who writes the flash offline can replace it | yes | no (needs a session with a key that may write R-MEM) |
| Root on the running router can replace it | yes | yes, while the device key may write R-MEM (see 5) |
| Secret? | no | no (public key; anyone with a session can read it) |

The **counter** is the part only the chip gives: the rollback floor is not stored on flash, so restoring an old overlay or
reflashing through the update path cannot lower it.

TROPIC01 does **not** verify the signature (it has no verify command); `ucert` on the router does.

## 4. Keys and certificates

- **Root key:** `usign`, Ed25519, kept offline by the operator; its public half is written into R-MEM slot 200 at
  provisioning. A second root (slot 201) allows a planned root change.
- **Release key:** the build's `key-build`, with `key-build.ucert` issued by the root (`ucert -I`). It can be replaced
  without touching any router: issue a certificate for the new key.
- **Revocation:** `ucert -I` also produces a revoker certificate. *To verify:* how `ucert -R` records a revocation and
  where the router should keep it (a TROPIC01 slot is the natural place).
- **Validity window:** a `ucert` certificate is valid from its issue time for one year by default, and the router's clock
  is wrong until NTP succeeds (it starts at the image build date). *To verify:* that `ucert -V` checks the window, and
  choose: long validity, or a check that tolerates an unset clock. Getting this wrong refuses good images.

## 5. Limits that stay

- **Not secure boot.** An attacker who writes the flash directly (serial console, U-Boot, a programmer) skips `sysupgrade`
  entirely. That is `cra-02-verified-boot`.
- **Root on the running router** can bypass the check (edit the scripts) and, while the device's pairing key may write
  R-MEM, replace the root key in the chip. R-MEM rights are all-or-nothing per pairing key (*measured*), so the vault's right to
  write slot 100 is also the right to write slot 200. Narrowing this needs the cra-04 key split (a key that may write R-MEM
  only behind a PIN, or only at provisioning).
- **`counter init` can be repeated** (*measured*): whoever may initialise counters can lower the floor. Removing that
  right from the device key (R-config, reversible) is the first step; burning it away in I-config is the irreversible one.
- **Fail closed:** with the chip unreachable or in alarm mode, no update is accepted. Recovery is failsafe mode or a
  console flash, both of which need physical access.
- **The chip is shared:** OpenVPN keeps a session open through the PKCS#11 module. The check stops OpenVPN while it reads the
  chip and starts it again (until the session broker, backlog 3.12, exists). The commit runs at boot before OpenVPN starts.

## 6. Steps

1. **Sandbox:** the router-side tool and hook, tested with OpenWrt's `usign`, `ucert` and `fwtool` and the fake chip.
2. **Build:** an OpenWrt patch that puts the security version into the image metadata, and the harness hook that places
   `key-build` and `key-build.ucert` (never committed). Host tool to create the root key and issue release certificates.
3. **Spare chip:** root key in R-MEM, counter initialised, signed image accepted, tampered and older images refused, commit
   after boot.
4. Later: the release key inside a TROPIC01 on the build side; the R-MEM write right moved off the device key (cra-04).
