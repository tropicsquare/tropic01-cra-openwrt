# cra-04-access-control — authentication and access control

**CRA Annex I, Part I, 2(d). Status: partial.**

## At a glance

| | |
|---|---|
| Requirement | CRA Annex I, Part I, 2(d): protection from unauthorised access, authentication |
| Status | partial: software tooling exists, no unit has been provisioned |
| Irreversible step | invalidating pairing slot 0 (needs explicit confirmation, see CLAUDE.md in this folder) |

## Role of TROPIC01

**Central, and the part that is not finished.** The chip only talks to a host that proves
knowledge of a *pairing key* held in one of four pairing slots. Today every unit uses the same
published engineering-sample key (slot 0), so anyone with that key can open a session with any
unit's chip. The fix is a per-unit pairing key written to a spare slot with slot 0 then
invalidated; the tooling for that exists, and it is deliberately not run, because invalidation
cannot be undone. The chip also enforces per-slot access rules (user access policy) for keys and
data that this feature will use once units are provisioned.

## What already works (lives in core/ and cra-03)

Each device has its own identity: a P-256 key generated inside TROPIC01
and an X.509 certificate stored in TROPIC01 R-MEM, used for OpenVPN
(AWS IoT mutual TLS is in the backlog).

## The gap

1. **Shared pairing key.** Every build talks to the chip with the same
   published `ENGINEERING_SAMPLES_01` pairing key
   (`core/libtropic-pkcs11/patches/003-use-engineering-sample-keys.patch`).
   Anyone with that key and bus access can open a secure session to any
   unit in the fleet.
2. **No privilege scoping.** Access is all-or-nothing per pairing slot;
   TROPIC01 User Access Privileges (UAP) are not configured.
3. **Private half on flash.** Even a per-unit pairing key is only as
   strong as where its private half is stored.

## What is implemented (software side, no chip writes)

The host-to-chip pairing key can now be per unit instead of shared, but
**no unit has been provisioned**: nothing has written a pairing slot or
invalidated slot 0, so on every unit the shared key still works.

- `host-tools/tropic01-gen-pairing-key.sh <serial> <build>/files <manifest>`
  creates a unit's X25519 pairing key pair into the image overlay
  (`/etc/tropic01/pairing.{key,pub,slot}`) and records the public half in an
  operator manifest outside git. Run per unit, before building its image.
- `tropicctl` (`--key auto`) and `libtropic-pkcs11` (core patch 007) read
  those files when they exist and use that key and slot. With **no** key
  files they warn (stderr / syslog) and keep the shared engineering-sample
  key, so unprovisioned bench units still work. With key files that are
  present but unusable they **fail**; they never fall back silently.
- `tropicctl pairing read <slot> --out <file>` reads the public pairing key of a slot
  (0-3). Read-only; useful to see which slots are populated.
- `tropicctl pairing write <slot> --in <file> --yes-write-pairing-slot` writes a 32-byte public key
  into an empty slot 1-3 and reads it back. It refuses slot 0, a non-empty
  slot, a file that is not 32 bytes, and any call without the literal
  `--irreversible`. Run it with `LT_UTIL_FORCE_SHARED_KEY=1` so the session
  uses the shared key. **It has been built but never run against a chip**, and
  nothing in this repo calls it.

## Current decision (2026-10-05)

The unit keeps using the shared engineering-sample ("test") key in slot 0,
which stays valid. **No per-unit key has been provisioned and slot 0 has not
been invalidated.** A key pair for the bench unit `OWO-BENCH-01` was generated
and backed up outside the repo but was never written to the chip, so it
protects nothing. Provisioning stays deferred until it is decided on a
specific unit, ideally a spare chip first.

## Verified on hardware (OpenWrt One, TROPIC01-ES, 2026-10)

Run with `tests/hardware/bench.py cra04`, which pushes `tropicctl` to
`/tmp` (nothing flashed, nothing written to the chip): with no key files the shared
key still works; pairing slot 0 is populated and slots 1-3 are **empty**
(`LT_L3_PAIRING_KEY_EMPTY`), so no per-unit key has ever been provisioned on this unit
and slot 1 is free; unusable key files are refused with no fallback; a wrong but
well-formed key is used and rejected by the chip; removing the files restores access.
**Not run on hardware:** the `libtropic-pkcs11` path (it builds, and shares the loader
logic, but no PKCS#11 consumer was available on the unit to exercise it) and any
successful session with a *per-unit* key, which needs the provisioning step below.

## What this does not fix

- **Nothing is provisioned.** Until a per-unit public key is written into a
  pairing slot and slot 0 is invalidated (both below), the shared key still
  opens every chip. Generating keys and teaching the software to use them
  closes nothing by itself.
- **The private half sits on the router's flash**, in cleartext, root-only.
  That ends *fleet-wide* sharing (one unit's key opens only that unit) but
  not extraction from a flash dump of the same unit. A stronger home needs
  the encrypted overlay of cra-05, or a TPM-style host element, neither of
  which exists here.
- No UAP scoping yet (below).

## Plan

Phase 1, above, is done. The rest is **irreversible per unit** and needs an
explicit decision each time, on that unit:

1. At provisioning, write a per-unit pairing key into slot 1 or higher.
2. Invalidate slot 0 (the factory/engineering key).
3. Configure UAPs per slot, e.g. a telemetry slot that can sign but cannot
   read or write R-MEM.
4. Where the host-side private pairing key lives: decided for now (root-only file on flash, see "What this does not fix"); revisit with cra-05.

Backlog items for this feature (provisioning, UAP scoping, private-key placement, the per-unit
versus fleet image) are in the central [CRA backlog](../../docs/cra/backlog.md), section 4.

## Done means

- [ ] two units provisioned from the same image have different pairing keys
- [ ] slot 0 is invalidated after provisioning
- [ ] a slot scoped to signing is refused an R-MEM write
- [ ] the provisioning procedure is documented, with its irreversible steps marked
