# cra-04-access-control — authentication and access control

**CRA Annex I, Part I, 2(d). Status: partial.**

## At a glance

| | |
|---|---|
| Requirement | CRA Annex I, Part I, 2(d): protection from unauthorised access, authentication |
| Status | partial: `tropic01-pairing` provisions a per-unit key and restricts the shared key; run on a spare chip and on the bench unit (2026-10-11); slot 0 is not invalidated anywhere |
| Irreversible step | writing a pairing slot (`tropic01-pairing`); invalidating slot 0 (not done, a separate decision). See CLAUDE.md in this folder |

## Role of TROPIC01

**Central, and the part that is not finished.** The chip only talks to a host that proves
knowledge of a *pairing key* held in one of four pairing slots. Units that are not provisioned
use the same published engineering-sample key (slot 0), so anyone with that key can open a session
with their chip. The fix is a per-unit pairing key in a spare slot, with the shared key's rights
taken away in the R-config. `tropic01-pairing` does this. It was run on a spare chip and on the
bench unit. There slot 0 is restricted, not invalidated: invalidation cannot be undone and is a
separate decision. The chip also enforces per-slot access rules (user access policy) for keys and
data; beyond removing the shared key's rights, this feature does not use them yet.

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
2. **Little privilege scoping.** `tropic01-pairing` takes all rights from
   the shared key; nothing else is scoped. R-MEM rights are all-or-nothing
   per pairing key (measured).
3. **Private half on flash.** Even a per-unit pairing key is only as
   strong as where its private half is stored.

## What is implemented

The host-to-chip pairing key can be per unit instead of shared. The spare
chip (slot 3) and the bench unit (slot 1, 2026-10-11) are provisioned. On the
bench unit the shared key is refused. On the spare chip the restriction was
verified and then undone, so its R-config is the original again. Slot 0 is
not invalidated anywhere. Other
units still use the shared key.

- `host-tools/tropic01-gen-pairing-key.sh <serial> <build>/files <manifest>`
  creates a unit's X25519 pairing key pair into the image overlay
  (`/etc/tropic01/pairing.{key,pub,slot}`) and records the public half in an
  operator manifest outside git. Run per unit, before building its image.
- `tropicctl` (`--key auto`) and `libtropic-pkcs11` (core patch 007) read
  those files when they exist and use that key and slot. With **no** key
  files they warn (stderr / syslog) and keep the shared engineering-sample
  key, so unprovisioned bench units still work. With key files that are
  present but unusable they **fail**; they never fall back silently.
- `tropicctl pairing read <slot> --out <file>` reads the public pairing key of a slot (0-3), read-only.
- `tropicctl pairing write <slot> --in <file> --yes-write-pairing-slot` writes a 32-byte public key into an empty
  slot 1-3 and reads it back; it refuses slot 0 and a slot that is not empty. One way. Run on a spare chip (see below).
- **`tropic01-pairing`** (`tropic01-pairing/`, a package of this feature) runs the whole tier-0 provisioning on top of
  `tropicctl`: `status`, `provision`, `undo`. Every chip write is off by default and has its own flag
  (`--yes-write-pairing-slot`, `--yes-rewrite-r-config`). It refuses chip firmware older than 2.0.0 (I-Memory writes can fail
  silently there) unless `--allow-old-firmware` is given; the slot is still read back. It writes the unit's public key into its slot and reads it back, proves the per-unit key opens a session,
  then rewrites the R-config so the shared slot-0 key loses its rights (erase before write, first configuration saved in
  `/etc/tropic01/rconfig-before.cfg`), restarts the chip (it loads its R-config at start-up), and checks that the shared key
  is refused and the per-unit key works; if not it restores the saved configuration. **Slot 0 is restricted, not
  invalidated.** The sandbox test (`tests/sandbox/pairing.sh`, 65 checks against the real `tropicctl` with its fake chip)
  covers the gates, and the script was run end to end on a spare chip (hardware report, 2026-10-11: provision into slot 3, shared key refused, per-unit key works, re-run, undo). The bench unit got its per-unit key on 2026-10-11 (slot 1, shared key restricted).
- `/etc/tropic01/` is on the `sysupgrade` keep list (from the `tropicctl` package), so an upgrade that keeps settings keeps
  the per-unit key. A settings backup then contains the private pairing key: treat backups as credentials.

## Current state (2026-10-11)

The bench unit has a per-unit key in slot 1. Its shared key is restricted:
refused for R-MEM, ECC and the configuration. Slot 0 stays valid
(invalidation is backlog 4.2). Other units still use the shared key.

## Read-only survey (2026-10-05, before provisioning)

Run with `tests/hardware/bench.py cra04`, which pushes `tropicctl` to
`/tmp` (nothing flashed, nothing written to the chip): with no key files the shared
key still works; pairing slot 0 is populated and slots 1-3 are **empty**
(`LT_L3_PAIRING_KEY_EMPTY`), so no per-unit key has ever been provisioned on this unit
and slot 1 is free; unusable key files are refused with no fallback; a wrong but
well-formed key is used and rejected by the chip; removing the files restores access.
This survey did not cover the `libtropic-pkcs11` path or a session with a *per-unit*
key. Both were run later: on the bench unit (2026-10-11) the PKCS#11 module, the vault
and the OpenVPN tunnel work with the per-unit key (hardware report).

**`tests/hardware/bench.py cra04` and `suite` are for unprovisioned units.** They plant and
delete test key files in `/etc/tropic01/`, so they refuse when `pairing.*` exists there.

## What this does not fix

- **Most units are not provisioned.** On those the shared key still opens
  the chip. On a provisioned unit the shared key is restricted, not
  invalidated. Generating keys alone closes nothing.
- **The private half sits on the router's flash**, in cleartext, root-only.
  That ends *fleet-wide* sharing (one unit's key opens only that unit) but
  not extraction from a flash dump of the same unit. A stronger home needs
  the encrypted overlay of cra-05, or a TPM-style host element, neither of
  which exists here.
- No scoping beyond removing the shared key's rights (below).

## Plan

Phase 1, above, is done. Each step below needs an explicit decision on that unit:

1. Write a per-unit pairing key into slot 1 or higher (one way). Done on the
   spare chip and the bench unit.
2. Take the shared key's rights away in the R-config (reversible). Done on the
   bench unit; done, verified and undone on the spare chip.
3. Invalidate slot 0 (the factory/engineering key). Irreversible; not done; a
   separate decision.
4. Configure UAPs per slot, e.g. a telemetry slot that can sign but cannot
   read or write R-MEM.
5. Where the host-side private pairing key lives: decided for now (root-only file on flash, see "What this does not fix"); revisit with cra-05.

Backlog items for this feature (provisioning, UAP scoping, private-key placement, the per-unit
versus fleet image) are in the central [CRA backlog](../../docs/cra/backlog.md), section 4.

## Done means

- [ ] two units provisioned from the same image have different pairing keys
- [ ] after provisioning, the shared key is refused for R-MEM, ECC and the configuration (restricted; invalidation is backlog 4.2)
- [ ] a slot scoped to signing is refused an R-MEM write
- [ ] the provisioning procedure is documented, with its irreversible steps marked
