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

## 3. What TROPIC01 really adds here

**What it does not add.** The signature check (`ucert`) works the same with the root key in the root filesystem. A
network attacker sending a bad image is stopped by the signature wherever the key is kept. Someone who writes the flash
directly bypasses `sysupgrade` altogether (only secure boot, cra-02, stops that). And today root on the running router can
switch the check off, rewrite the key slots and re-initialise the counter, because the device's pairing key holds those
chip rights, and its private half is on the same flash.

| Property | Floor and root key on flash | In TROPIC01 |
|---|---|---|
| Survive a factory reset, an overlay wipe, a settings restore, a reflash through `sysupgrade` | no (overlay), or only in a dedicated partition | **yes**: they stay with the chip |
| The floor only goes up | a software rule; any flash write can lower it | **enforced by the chip** (the counter only decrements), except through `counter init` |
| The root key belongs to the unit, not to the image | no: every image brings its own keys | **yes**: rotation and per-owner roots without new images |
| Root on the router (or someone holding the flash) can lower the floor or replace the root key | yes | **yes today; no once the device key loses those rights** (section 7) |
| Secret? | no | no (public key; the counter is readable) |

So the value is: (1) **a rollback floor that a reset cannot undo**, which works today: a unit that was reset or restored
still refuses older signed images; (2) **a floor and a root key that software on the device cannot change**, which needs
section 7 and is the one thing flash storage cannot offer; (3) per-owner roots, an operational feature. Without section 7
the root key in R-MEM is no stronger than a key in a dedicated flash partition, and the docs must not say otherwise.

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

## 7. Least privilege for the device key (backlog 10.6)

The chip decides per pairing key which commands it may run (*measured*: the access objects work as libtropic describes; a
rewritten R-config takes effect after a chip restart). The device's runtime key (its private half on the router's flash)
should keep only what the running system needs:

| Right | Runtime key | Why |
|---|---|---|
| R-MEM read, ECC read and sign, random, counter get, ping | keep | the vault opens, OpenVPN signs, the update check reads keys and floor |
| counter update | keep | the boot commit raises the floor (it can only go up) |
| **counter init** | **remove** | the only way to lower the floor |
| **R-config write and erase** | **remove** | otherwise any removed right can be given back |
| **I-config write** | **remove** | |
| **pairing key write and invalidate** | **remove** | otherwise root writes its own key into a free slot and uses that key's rights |
| ECC generate, store, erase | remove after enrolment | only needed when the VPN identity is created |
| R-MEM write and erase | **decision** | needed by the vault (init, destroy) and to install update keys; R-MEM rights cover every slot at once, so keeping them leaves the root key slots writable |

Every pairing slot that holds no key gets no rights at all (a key written into it later would otherwise inherit them).

**Who can still change anything:** an **owner key** in another pairing slot, with every right, whose private half is not on
the router (kept by the operator, brought in only for maintenance). Without an owner key, removing the configuration rights
is effectively permanent. Burning the same bits in the I-config makes it permanent on purpose (one-way, firmware 2.0.0 or
newer, read back).

Decided (2026-10-11): the runtime key keeps R-MEM write; the owner key is offline, in pairing slot 3; R-config only
(reversible). *Measured* on the bench chip: the runtime key is refused counter init, configuration rewrite, pairing write and ECC
generation; the vault, the PKCS#11 module and the OpenVPN tunnel work with it; the owner key undoes and reapplies the profile.
Consequence of the R-MEM decision: the floor is now out of the device's reach, the root key slots are not.
