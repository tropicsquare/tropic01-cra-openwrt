# Per-unit pairing key: where it lives and how it is protected

Design draft for backlog items 4.1 to 4.5. **Nothing here is implemented or has been run on a chip.** Statements
are marked *from the documents* (Tropic Square's PIN Verification Application Note ODN_TR01_app_002 and its
hardened-authentication example), *from the libtropic 4.2 headers*, or *to verify on a spare chip*. The vendor
documents are not copied into this repository; read them from Tropic Square.

## 1. Problem

TROPIC01 only talks to a host that proves knowledge of a **pairing key** held in one of four pairing slots. Today
every unit uses slot 0, which holds the published engineering-sample key, so anyone with that key and access to the
chip's SPI bus can open a session and read R-MEM (the vault key, certificates) or use the ECC keys. A per-unit
key in another slot fixes that only if (a) slot 0 stops being useful to an attacker and (b) the new key's private
half is not trivially stolen. This document is about both.

| Attacker | Gets | Today | Goal |
|---|---|---|---|
| Knows the shared key, has the chip (desoldered or on a bench) | chip only | full access | no access |
| Dumps the flash of a unit, no chip | flash only | nothing useful | unchanged |
| Holds the whole unit (flash and chip) | everything stored in the clear | full access | PIN-gated (tier 1 below) |
| Remote root on the running router | a live session | full access | **not stopped**; limit with per-key access rights (4.3) |

## 2. What the vendor documents give us

*From the documents.*
- **MAC-and-Destroy** releases a 32-byte key *k* from a PIN. The chip holds up to 128 slots, one per allowed attempt;
  each wrong attempt destroys a slot, and a correct PIN re-initialises them. The attempt counter lives in the chip's
  slots, not in host code, and the scheme avoids branching to resist fault injection.
- The host stores only a ciphertext of the master secret and a tag. **That host storage needs no protection**: the
  chip's own keyed functions make the released value unreproducible outside the chip.
- *Additional data A* can be mixed into the PIN so the key also depends on a value an attacker has to extract
  separately from the hardware.
- Several PINs per key, and a **wiping PIN** that destroys the key, are specified extensions.
- Hardened-authentication example (wallet): bootstrap a session with a default pairing key, run MAC-and-Destroy
  with the PIN, use the released secret to decrypt the private half of a **second pairing key**, close the session,
  open a new one with that key, and set access rights so ECC and R-MEM slots are reachable **only** with it. A lost
  PIN then blocks all ECC and R-MEM operations. The example notes a PIN cannot simply be reset because the pairing
  key lives in I-Memory (see 3 for how the scheme may avoid that).

## 3. Chip facts that shape the design

*From the libtropic 4.2 headers.*
- Access is per command: there is a configuration object for each operation (R-MEM read, write, erase; ECC
  generate, read, sign; random; MAC-and-Destroy; pairing-key read, write, invalidate; configuration read and
  write) that says which pairing keys may use it.
- **R-config can be rewritten, but only after erasing it.** Writing R-config without erasing first can brick the
  chip (erratum OI_TR01_ERR_2026010800: permanent Alarm Mode). Read the Configuration Objects Application Note
  ODN_TR01_app_006 first.
- **I-config only flips bits from 1 to 0** (irreversible), lives in I-Memory, and fails silently on CPU firmware
  older than 2.0.0 outside -20 to 85 C.
- A pairing slot is written once (I-Memory) and can be invalidated, also irreversibly. Slots 1 to 3 are empty.
- The R-config write permission is itself a configurable right: **if slot 0 keeps it, an attacker with the shared
  key can erase and reset every restriction.** Restricting slot 0 therefore includes taking that right from it.

*Measured on the bench chip (TROPIC01-ES, read-only survey 2026-10-10, see the hardware report).* CPU firmware 0.1.2
and SPECT 0.3.1 (read as libtropic's example reads the bytes: older than 2.0.0, so I-Memory writes can fail silently and every such write must be read back); I-config untouched (all ones); R-config and every access object at `0xffffffff`, so **today every
pairing key may run every command on every target**. Per libtropic's headers an access object is four bytes, one per
group of targets (ECC slots in groups of 8, MAC-and-Destroy and R-MEM slot groups, counters in groups of 4), with one
bit per pairing key in each byte. Rights are therefore scoped per command **and per slot group**, which is finer than
the table in section 5 shows (for example a key that may sign only with ECC slots 0 to 7).

## 4. Where does the host's private pairing key live?

| Option | Protects against | Cost |
|---|---|---|
| **A. Root-only file on flash** (what the tooling does today) | shared-key holders; chip-only theft | whole-unit theft and flash dumps expose it |
| **B. Encrypted under MAC-and-Destroy, PIN entered at boot** | A, plus whole-unit theft (limited attempts, then the key is gone) | the router cannot start unattended: services that need the chip wait for a person |
| **C. MAC-and-Destroy with a PIN-less secret** (no human) | adds nothing over A: the attempt limit only matters for a low-entropy secret | extra code for no gain; **rejected** |

**Recommendation: ship A as the default and design B as an opt-in "locked mode".** A router that serves a VPN or
telemetry has to boot without a person, so B cannot be the default; pretending otherwise would just move the key
into a file again. The two tiers must not weaken each other, and the slot plan below is chosen so tier 1 can be added
later without rewriting tier 0.

## 5. Slot plan

Slots 1 to 3 are the scarce resource (one write each, no undo), so the plan uses at most three and leaves one spare.

| Slot | Role | Tier 0 (default) | Tier 1 (locked mode, later) |
|---|---|---|---|
| 0 | shared engineering key | rights removed (section 6), **not** invalidated at first | unchanged |
| 1 | per-unit **device key**, private half in `/etc/tropic01/pairing.key` | all rights the product needs | rights reduced to MAC-and-Destroy only: it can start a PIN check, nothing else |
| 2 | per-unit **protected key**, private half encrypted under the MAC-and-Destroy secret | not used | holds ECC, R-MEM, random and configuration rights |
| 3 | spare | not used | not used (keeps one write for a recovery or rotation design) |

Why two keys in tier 1: the PIN check itself needs an open session, so some key must be usable *before* the PIN is
known. That key (slot 1) may only run MAC-and-Destroy. An attacker who has it can burn the PIN attempts (denial of
service, never disclosure), which is the same trade-off the vendor example makes with its default key.

## 6. Retiring the shared key: restrict first, invalidate last

Invalidating slot 0 is permanent and can lock a chip for good if the per-unit key is lost. Taking all useful rights
away from slot 0 through the configuration objects gives the same practical protection and, because R-config can be
erased and rewritten by the remaining key, can be undone. Plan: restrict on a spare chip, run the whole test suite,
live with it, and decide separately whether invalidation is worth its risk (backlog 4.2). *To verify:* the layout
of the access-right bits, the factory defaults on this engineering sample, and that slot 1 can still rewrite R-config
after slot 0 loses that right (otherwise the chip would be stuck in its restricted state).

## 7. Flows

**Provisioning (tier 0), per unit, spare chip first.** Every chip write needs explicit confirmation on that unit.
1. Read the firmware version and the four pairing slots; slots 1 to 3 must be empty.
2. Generate the per-unit key pair on the host (`tropic01-gen-pairing-key.sh`), keep the private half out of git.
3. With the shared key: write the public half to slot 1 (irreversible), **read it back and compare**.
4. Open a session with the new key and prove it works (chip ID, R-MEM read).
5. Erase R-config, write the restricted configuration in one go, read it all back. Slot 0 can now do little more than
   read the chip identity. Prove the shared key is refused for R-MEM, ECC and configuration.
6. Install the key files in the image; the existing loader already refuses to fall back silently.

**Locked mode (tier 1), later.** New PIN setup follows the vendor procedure (master secret, per-slot initialisation,
ciphertexts in host flash). Boot: open a session with slot 1, run the PIN check, decrypt the slot-2 private key,
close the session, open a new one with slot 2. Services that need the chip (vault unlock at S45, OpenVPN at S90)
start only after that; until then the router is reachable but the vault is locked and the tunnel is down. This
interacts with the SPI-session broker (backlog 3.12) and with PIN-based full-overlay encryption (backlog 5.4), which
should share one PIN prompt.

**Changing the PIN.** The vendor note allows supplying the master secret to the new-PIN procedure; if the same
secret is reused, the released key *k* does not change and the slot-2 ciphertext stays valid, so a PIN change would
not consume a pairing slot. *To verify.* A forgotten PIN is unrecoverable by design: ECC and R-MEM stay blocked, so
the VPN key and the vault are lost and must be recreated after a factory reset.

**Wiping PIN.** The vendor extension (a second PIN that destroys the key) fits a "panic" use case; it adds
complexity and is out of scope until tier 1 exists.

## 8. What this does not fix

- A remote attacker with root on a running router can still use the chip through the open session (4.3 narrows
  what that session may do, for example signing but no R-MEM reads).
- Tier 0 does not stop someone who holds the whole unit: the key file is in the flash they hold.
- Tier 1 protects the pairing key only; everything else on flash stays unencrypted unless backlog 5.4 is done.
- Side-channel or fault attacks on the chip are the vendor's domain; the scheme's threat model is in the vendor note.

## 9. Open questions to answer on a spare chip

1. ~~Factory defaults of the access-right bits~~ (answered: all permissive on the bench chip). Still open: confirm the
   byte and bit layout from the headers by changing a spare chip, and whether MAC-and-Destroy rights are per slot group
   and per pairing key as the headers suggest.
2. That the R-config erase-then-write cycle is safe on this chip and firmware, and what the chip does between the
   erase and the write (it must not be left permissive).
3. ~~Chip CPU firmware version~~ (answered: **0.1.2**, older than 2.0.0, so I-Memory writes can fail silently). Still open: the
   temperature rule (-20 to 85 C for I-Memory) and whether to update to the 2.2.0 firmware first, which is itself a risky step
   and may change the behaviour the design relies on.
4. Whether a rewritten R-config survives power cycles and reflashes (it is on the chip, not the router).
5. How libtropic's `lt_mac_and_destroy()` and the hand-written PIN code in `lt-util` (backlog 3.5) compare, and which
   one tier 1 should use.
6. PIN length, retry limit and the number of MAC-and-Destroy slots to reserve; the vault does not use any.

## 10. Proposed steps

1. **Sandbox (no chip): done as a draft.** `tropic01-pairing` (`status`, `write-slot`, `restrict-shared`,
   `restore-config`) and `tests/sandbox/pairing.sh` (79 checks against a fake chip with per-key access rights).
   Every chip write is off by default (dry run) and needs its own `--yes-...` flag; writes are read back; the
   template is refused if it leaves slot 0 any right except PING or locks the per-unit key out of the
   configuration; the previous config is saved before a rewrite. **The script cannot run on a real chip yet:** it
   needs `lt-util` commands that do not exist (below), and the fake's access-right model is an assumption.
2. **Read-only survey of the bench chip: done** (`lt-util -x -f`, `-x -r`; results in section 3 and the hardware report). Next: read the pairing slots 1 to 3 the same way before any write.
3. **Spare chip:** run provisioning steps 1 to 5 above, with a hardware report in `docs/cra/`.
4. **Bench unit:** only after the spare chip passes, one confirmation per irreversible step.
5. **Tier 1:** a separate design review after the session broker (3.12) is decided.

## 11. What `lt-util` still has to learn

`tropic01-pairing` calls these, which do not exist yet and must be written and tested read-only first:

| Command | Does | Rule |
|---|---|---|
| `lt-util -x -r` | **exists (patch 006)**: prints every R-config and I-config object as `R|I NAME 0xVALUE` on stdout, raw 32-bit words (notices go to stderr); `-x -f` prints the firmware version bytes | read-only; used for the survey. The `NAME slots` form the draft script expects is a translation layer still to be written on top of these words |
| `lt-util -x -w <file> --rewrite-r-config` | erases the R-config, writes the file, reads it back | **always erase before write** (libtropic erratum OI_TR01_ERR_2026010800); refuses without the flag; if the write fails after the erase, say so and exit non-zero |
| `lt-util -k -r N <file>` | already exists; on an empty slot its error must contain `SLOT_EMPTY` | the script treats any other error as "cannot tell" |
| the access-denied error | must be recognisable (`UNAUTHORIZED`, `NOT_ALLOWED` or `ACCESS` in the text) | the script uses it to tell "restricted" from "chip down" |

The translation between these `NAME slots` lines and the chip's real configuration objects is part of that work and
depends on the survey (section 9).
