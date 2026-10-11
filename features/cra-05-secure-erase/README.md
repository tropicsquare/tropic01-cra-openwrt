# cra-05-secure-erase — an encrypted vault with crypto-erase

**CRA Annex I, Part I, 2(m). Status: partial. The vault is implemented,
sandbox-tested and verified on an OpenWrt One.**

> "...the possibility for users to securely remove on a permanent basis all
> data and settings"

## At a glance

| | |
|---|---|
| Requirement | CRA Annex I, Part I, 2(m): users can securely remove all data and settings |
| Status | partial: one encrypted folder (`/vault`), not the whole device |
| Verified | on an OpenWrt One, including destroy and refusal to open an old copy of the image |
| Irreversible step | `tropic01-vault destroy --yes-destroy-vault` |

## Role of TROPIC01

**Central.** The chip's random number generator makes the 32-byte vault key, and R-MEM slot 100
stores it, so the key is never on flash. A copy of the flash is therefore useless without the
chip. **Crypto-erase** is erasing that one slot: after that, even the original ciphertext cannot
be decrypted by anyone. The chip does not encrypt the data itself (LUKS2 in the kernel does)
and it does not protect the unlocked vault from root on a running system.

## Scope decision

Encrypting the whole overlay (every setting, key and file) needs an early-boot
unlock hook and either a kernel change (fscrypt) or a loop-mounted LUKS
overlay. This feature takes the smaller route: **one dedicated encrypted
vault, mounted at `/vault`, and only what is stored there is protected.** The
rest of the overlay (configuration, Wi-Fi keys, `/etc/shadow`, SSH keys, logs)
is **not encrypted**. A factory reset still wipes the overlay the usual way;
only vault data gets cryptographic erasure. That is a partial answer to 2(m),
and the README of this feature is where that stays honest.

## How it works

- **Container.** A LUKS2 image file (default 16 MB, 15 MiB usable) at
  `/etc/tropic01/vault.img`, formatted ext4, mounted at `/vault` (`0700`).
  LUKS2 with a 1 MiB header and one keyslot; the PBKDF is set to its minimum
  because the key is already 32 random bytes.
- **Key.** 32 bytes from TROPIC01's RNG, stored in **R-MEM slot 100** and nowhere
  on flash. While a command runs it exists only as a `0600` file in RAM, removed
  on exit. R-MEM 32-511 is general purpose; 1-31 hold factory certificates and are
  refused; 0 is not used (backlog 3.14).
- **Crypto-erase.** `tropic01-vault destroy --yes-destroy-vault` closes the
  vault, **erases slot 100**, checks that the slot no longer returns data, and
  only then deletes the image. If the erase or the check fails the image is kept
  and the command says the vault is NOT destroyed.
- **Boot.** `/etc/init.d/tropic01-vault` (S45) unlocks it early, before OpenVPN
  takes TROPIC01's single SPI session at S90. If it cannot unlock (chip busy or
  absent, slot empty), the vault stays locked, `/vault` is not mounted and the
  system boots normally.
- **Commands.** `tropic01-vault status | init | open | close | destroy`. `init`
  refuses if an image exists or if slot 100 is not empty.

## What this protects, and what it does not

Protects: data in `/vault` against **someone who has only a copy of the flash**
(the key is not on flash), and, after `destroy`, against anyone, because the only
copy of the key is gone.

Does **not** protect:

- **Anything outside `/vault`.** Nothing moves data into it automatically; apps
  and the owner have to put data there.
- **Against someone who holds the whole unit.** The chip is reachable with a
  pairing key. Units that are not provisioned answer to the shared
  engineering-sample key; provisioned units (cra-04) answer to a per-unit key that
  is on the same flash. Either way the holder can read R-MEM slot 100 and decrypt
  the vault. Closing that needs the PIN-protected pairing key (cra-04 tier 1),
  which the vault does not use.
- **While running.** Root can read the unlocked vault and the key in RAM.
- **NAND wear/physical remnants.** Deleting the image does not guarantee the old
  ciphertext blocks are gone; that is the point of crypto-erase, and it only holds
  if the key really is erased. Check the datasheet for R-MEM erase guarantees before
  making stronger claims.

## Limits to know

- **One SPI session.** `init`, `open` and `destroy` need the chip; they retry while
  it is busy. If OpenVPN or the AWS client holds the session, stop them first.
- **Fixed size.** The image is allocated up front (default 16 MB); resizing is not
  implemented.
- **Double filesystem** (ext4 inside LUKS inside a loop file on UBIFS): extra write
  amplification on NAND.

## Testing

- Sandbox: the state machine with a fake chip and a fake `cryptsetup` — init creates
  the key and image and leaves no key file behind; a second init is refused; init
  refuses a non-empty slot without overwriting it; open reads the key back from the
  chip; a missing chip leaves the vault locked; `destroy` needs its flag, keeps the
  image if the erase fails, and only ever touches slot 100.
- On the host without root: the LUKS2 parameters (header size, usable space, the
  right key opens it, a wrong key does not).
- **On an OpenWrt One** (flashed image with `dm-crypt`; `tests/hardware/bench.py cra05
  --phase stack` plus manual steps with the chip):
  - the encryption stack with a throwaway RAM image and a random key, no chip access:
    format with the vault's parameters, open (loop + dm-crypt), mount ext4, the
    plaintext marker is **not** in the image file, reopening returns the data, a wrong
    key is refused (6/6);
  - `tropic01-vault init` against the real chip: R-MEM slot 100 was empty, now holds
    32 bytes; the factory certificate in slot 1 (299 bytes) is untouched; no key file
    is left in RAM or on flash; the image is `0600`;
  - data written into `/vault`, `close`, a copy of the image taken off the board
    starts with the LUKS magic, is LUKS2 / `aes-xts-plain64` with a 1 MiB data offset,
    and does not contain the plaintext marker; `open` returns the data;
  - **fail closed:** with the configured slot pointing at a slot with no key (101),
    `open` refuses and the vault stays locked;
  - **reboot:** the S45 init script unlocked the vault by itself and the data was
    intact; no key file left in RAM;
  - vault events (`created`, `locked`, `unlocked`) reach the cra-06 security log;
  - usable space is about 10 MB of the 16 MB image (1 MiB LUKS header plus ext4
    overhead); size the image accordingly.
- **Repeated on the final image (2026-10-05):** `init`, data, lock/unlock, then a copy of the encrypted image
  was taken off the board, `destroy` erased slot 100 (slot 1 stayed 299 bytes), and the **old copy was put
  back on the router: the device refused to open it** (`could not read the vault key from TROPIC01 slot
  100`, nothing mapped or mounted). This is the key-only case: the ciphertext was present, only the key was
  gone.
- **Repeated on a clean rebuild (2026-10-11):** `init` (key in slot 100, no plaintext in the image), lock,
  unlock, `destroy` without its flag refused, `destroy` erased slot 100 and deleted the image, and the
  **old image put back was refused**.
- **`destroy` on the real chip:** before, the vault was open and slots 100 and 1 held 32
  and 299 bytes; after, slot 100 reads `LT_L3_R_MEM_DATA_READ_SLOT_EMPTY`, the image
  file is gone, nothing is mapped or mounted, `open` refuses, slot 1 (the factory
  certificate) is still 299 bytes, and the event is in the cra-06 security log.

## Backlog

Open work (a decommission hook, a bigger vault, and full
writable-storage encryption unlocked by a TROPIC01 MAC-and-Destroy PIN) is tracked in the
central [CRA backlog](../../docs/cra/backlog.md), section 5.

## Done means

- [x] vault with a key held only in TROPIC01 (implemented, sandbox-tested)
- [x] crypto-erase command that erases the key first and verifies it
- [x] verified on hardware: unlock at boot, data unreadable from a flash copy
- [x] verified on hardware: the key is erased from R-MEM and the vault is gone
- [x] factory identity survives a destroy (slot 1 still 299 bytes after it)
- [x] this README says what is not covered
