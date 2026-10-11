# Hardware test report

What was tested on real hardware for the branch that adds core, cra-01, cra-03/04 (software side),
cra-05 and cra-06, how, and what was **not** tested. Written to be pasted into the pull request.

## Setup

- **Board:** OpenWrt One (MediaTek MT7981), TROPIC01 engineering sample on the mikroBUS socket
  (`lt-util -i`: chip ID version `0x00000001`, SPI device `/dev/spidev2.0`, CS on GPIO 25).
- **Image:** built from the committed tree, `make build`, flashed clean (`sysupgrade -n`) from failsafe.
  SHA-256 `bf871831da4decde597d5092d001023d8ad0fe0e3628476e7052ded4d18dc4ee`, 15.2 MB, OpenWrt snapshot
  r32644, kernel 6.12.63. Date: 2026-10-05.
- **Harness:** `tests/hardware/bench.py` drives the USB debug console prompt by prompt and runs checks over a
  wired link (192.168.9.0/24, PC to the router's LAN port). See [tests/hardware/README.md](../../tests/hardware/README.md).
- **Not in CI:** CI has no board. `make lint`, the patch check against the pinned upstream and a clean-checkout
  image build run in CI; the secret scan was run locally as a pattern scan of the whole history (no `gitleaks`
  binary here).

## Results

`bench.py suite` on the flashed final image: **56 checks, 0 failed.**

| Step | Checks | What it covered |
|---|---|---|
| web | 6 | SSH bound to the LAN address only; HTTP and HTTPS answering (LuCI); rpcd rejects an empty password; dropbear bound to `lan`, password auth on |
| cra-04 | 13 | the `lt-util` installed in the image; the pairing-slot write command refuses without `--irreversible`, for slot 0 and for a wrong-size file, **before any chip access**; shared key still works with no key files; slots 1-3 empty, slot 0 populated; unusable key files refused with no silent fallback; a wrong well-formed key is rejected by the chip; access restored afterwards |
| cra-05 stack | 6 | LUKS2 on the board with a throwaway RAM image and a random key (no chip access): format, loop plus dm-crypt open, ext4 mount, no plaintext in the image file, reopen, wrong key refused |
| cra-06 | 18 | package installed in the image: real SSH and LuCI logins (failed and successful) from the PC recorded; a config edit and a service enable/disable recorded by name only; size cap; opt-out stops recording; stored lines kept; `clear`; a copy of the log contained neither the real nor a wrong password |
| cra-01 | 13 | a planted empty-password hash cleared by the boot-time guard **before** any login; empty password refused on every attempt and the session closing after five; real password accepted; `ttylogin` enforced and repaired; first-boot warnings removed |

Steps run by hand because they write or erase on the chip (each with an explicit go-ahead, slot 100 only):

- **cra-05 vault, final image:** `tropic01-vault init` (R-MEM slot 100 was empty, now 32 bytes; factory
  certificate in slot 1 untouched at 299 bytes; no key file left); data, lock and unlock; a copy of the encrypted
  image taken off the board; `destroy` (slot 100 empty again, slot 1 unchanged, image removed); the **old copy put
  back and the device refused to open it** because the key is gone. Earlier on a previous image: image copy shows
  LUKS2 `aes-xts-plain64` with no plaintext, fail-closed when the key slot has no key, automatic unlock at S45
  after a reboot.
- **cra-03 OpenVPN, final image:** the chip's certificate in R-MEM slot 1 (`CN=tropic01-client`) verifies against the
  CA of the target VPN; OpenVPN with the key in TROPIC01 over PKCS#11 brought up `tun0` in about 5 s, 4 of 4 pings through
  the tunnel (about 8 ms), the process held the SPI device; after a reboot with the instance enabled the tunnel came up by
  itself at 48 s of uptime (3 of 3 pings). The VPN configuration is the owner's private file; it was removed from the unit
  afterwards.

- **cra-03 enrolment, spare slots (2026-10-05, each chip write approved, ECC slot 4 and R-MEM slot 5 only):** a P-256 key
  generated in ECC slot 4; a CSR signed by the chip through `pkcs11-provider` (self-signature valid, public key equals the
  chip's); signed by a throwaway CA (419 bytes, fits R-MEM); stored in R-MEM slot 5, read back identical and exposed by the
  PKCS#11 module with id `/04`; a signature made by the chip's key verified with the stored certificate; then both slots
  cleared. Fingerprints of ECC slots 0-1 and R-MEM slots 1-2 were identical before and after.

## Not tested

- **AWS IoT telemetry:** was failing before this branch and was not retested (backlog 3.1).
- **Provisioning:** no per-unit pairing key was written to the chip, slot 0 was not invalidated, no UAP was configured
  (deliberate; cra-04's irreversible steps are deferred). The `libtropic-pkcs11` per-unit key path was not exercised at runtime.
- **Not built:** verified boot and rollback (cra-02), reset integrity, the LuCI first-login gate (on a unit with no password,
  the first person on the LAN can still choose it through LuCI), TROPIC01-anchored log signing, PIN-unlocked storage encryption.
  See [backlog.md](backlog.md).
- **A clean-checkout build and CI** had not been run when this was written.

## Hazards found while testing

- The board stalled at its first boot stage after a `sysupgrade` reset and needed power **and** the USB debug cable fully
  unplugged for about 60 s; this repeated after every flash. Cause unknown (backlog 7.1).
- Two programs reading the serial port at once split the bytes and make a harness (or a person) think the board is dead.
- Pushing data through the serial console hung the board twice and is not offered by the harness.

## Reproduce

```sh
make build                                    # the image
# flash from failsafe: sysupgrade -n, then a full power-cycle
python3 tests/hardware/bench.py suite         # everything that needs no irreversible chip step
```

## libtropic 4.2.0 upgrade (2026-10-09)

The results above were produced with the older libtropic. After the upgrade to v4.2.0 (one libtropic for
`lt-util` and the PKCS#11 module, branch `feat/libtropic-4.2`) the image was rebuilt and flashed clean
(`sysupgrade -n`; the board stalled after the flash and needed a full power and USB unplug, as before).
SHA-256 `814e8fb05ad3b0da4f27391390c2d0f4af5391d1cf4a489cfa328d8db1ae6ec3`.

- `bench.py suite`: **56 of 56** checks pass (web 6, cra-04 13 with the `lt-util` installed in the image,
  cra-05 stack 6, cra-06 18, cra-01 13). The cra-04 checks cover chip ID, the secure session with the shared key,
  an R-MEM read (299 bytes), pairing-slot inspection and the per-unit-key refusal rules.
- A certificate request made with the chip's ECC slot 0 key through the PKCS#11 module and OpenSSL verifies on a PC
  ("self-signature verify OK"), which exercises the 4.x signing path (a 32-byte hash, signed once).
- **Found and fixed during the upgrade:** the first ported `lt-util` crashed (segmentation fault) on every chip
  command, because libtropic 4.x needs the application to supply the crypto context of the handle.

Not re-run in the first 4.2.0 run (done later, see below): the vault `init`/`destroy`, an OpenVPN tunnel
(the bench has no WAN link) and the AWS test. The chip's firmware has been compared with what 4.2 ships: it is **much older** (see the correction below).

### Clean-container build (2026-10-10)

The image was then rebuilt from a **fresh clone of the pushed branch inside the Docker builder image, with no
download cache**, and flashed clean (this time the board came back without a stall). SHA-256
`b87a1c459171065ea6d7937fcc82e729c12e2ffd10b79ceebad7ef2a8588b550`.

- `bench.py suite`: **56 of 56** again, and a CSR made with the chip's slot 0 key through the PKCS#11 module verifies
  on a PC.
- The clean build found two build-environment problems that cached downloads and a fuller host had hidden, both fixed:
  the pinned source hash of `libtropic-pkcs11` (and `lt-util`) did not reproduce on a fresh download, and the Debian
  builder image lacked the `ip` command that OpenVPN's `configure` needs (`iproute2` added).

### Vault and OpenVPN on the libtropic 4.2.0 stack (2026-10-10, clean-built image)

Run by hand because they write and erase the chip (R-MEM slot 100 only; slot 1 stayed 299 bytes throughout).

- **Vault:** `init` (slot 100 was empty, now 32 bytes; no key file left on flash or in RAM); data written, lock and
  unlock work; a copy of the image taken off the board is LUKS2 `aes-xts-plain64` and does not contain the plaintext
  marker; `destroy` erased slot 100 (reads empty again) and deleted the image; the **old image copy put back was
  refused** ("could not read the vault key from TROPIC01 slot 100").
- **Key really in the chip:** the 32 key bytes read from slot 100 do not occur in a copy of `/etc` and the whole
  persistent overlay (34 MB), nor in the vault image. No file containing a private key exists on the router.
- **OpenVPN:** the VPN client key is in ECC slot 0 and the certificate in R-MEM slot 1: the certificate's public key
  equals the chip's slot 0 public key, and the certificate verifies against the VPN's CA. The installed config
  contains only `pkcs11-providers` and `pkcs11-id` (no `key`, no `cert`). The tunnel came up (WAN link via the home
  network), the OpenVPN process holds `/dev/spidev2.0` and `/dev/gpiochip0`, the module log shows the handshake
  signature as "32-byte hash -> TROPIC01 key slot 0 ... 64 bytes produced by TROPIC01 hardware", and 4 of 4 pings
  to the VPN gateway succeeded (about 7 ms).
- **Honest limit:** a vault key in R-MEM is readable by anyone who can open a session with the chip, which today
  uses the shared engineering-sample pairing key (cra-04). The VPN's tls-crypt key (not the client private key) is
  stored as a `0600` file by design.

### tropicctl phase 1, read-only run on the bench chip (2026-10-10)

The new tool (`features/core/tropicctl`, aarch64 build, 88 KB) was copied to `/tmp` on the router with OpenVPN stopped,
run, removed, and OpenVPN restarted. Only read commands were run; nothing was written to the chip.

| Check | Result |
|---|---|
| `info chip` | the same CHIP_ID block `lt-util -i` prints (TROPIC01-ES) |
| `info fw` | `00 02 01 00` / `00 01 03 00`, which libtropic's own example reads as byte 3 . byte 2 . byte 1: **RISC-V firmware 0.1.2, SPECT firmware 0.3.1** (see the correction below) |
| `session test` | opens with the shared key; warns on stderr that no per-unit key exists |
| `pairing list` | slot 0 populated, slots 1 to 3 empty (the fake's default, now **measured**) |
| `config dump` | in legacy mode (`lt-util -x -r`) **byte-identical** to the earlier survey output |
| `rng get 16` | 32 hex digits |
| `mem list --range 0-10` | slots 0, 1 and 2 hold data (299, 299 and 383 bytes) |
| exit codes | an empty slot is 6, a bad argument is 2 |
| legacy mode | `lt-util -i`, `-x -f`, `-x -r`, `-k -r` behave like the old tool; `-k -r` on an empty slot returns 1 |

**Observation to investigate (backlog 3.14):** R-MEM slot 0 reads back **identical to slot 1** (same 299 bytes, the
certificate `CN=tropic01-client`), although the project documentation says slot 0 is read-protected on this silicon.
Either that certificate was also stored in slot 0 at some point, or reads of slot 0 are answered with slot 1's data.
Slot 2 holds a 383-byte self-signed certificate (`CN=openwrt-tropic01`), probably from the earlier AWS experiments.
Nothing was written to find out; a test on a spare chip should write a distinct value to slot 1 and read slot 0.

### tropicctl (all areas), read-only run on the bench chip (2026-10-11)

The cross-built binary was copied to `/tmp` over SSH with OpenVPN stopped, run, removed and OpenVPN restarted. The
serial console was silent, so the updated `bench.py cra04` could not be run; the same checks were run by hand over SSH.
Nothing was written to the chip. **The commands that write (ECC key generation and erase, pairing write and
invalidation, R-config rewrite, I-config burn, MAC-and-Destroy, counter init and update) were NOT run on hardware.**

| Check | Result |
|---|---|
| refusals before any chip access | `pairing write` without its flag, for slot 0, with a bad file; `ecc erase` and `config rewrite` without their flags; `mem read 9999`: exit codes 9, 9, 2, 9, 9, 2, and no key announced |
| `pairing write --dry-run` on empty slot 1 | passes all checks, says it would write, writes nothing |
| `info chip`, `info fw`, `info mode`, `info cert` | chip ID; `00 02 01 00` / `00 01 03 00`; `application`; **four certificates of 423, 581, 626 and 626 bytes** |
| `pairing list` | slot 0 populated, 1 to 3 empty |
| `ecc list` | slots 0 and 1 P-256, slots 2 and 3 Ed25519, all "generated"; the rest empty |
| `config dump --decode` | 54 raw lines plus decoded access groups; every access object `0xffffffff` (all four keys may do everything) |
| `counter get 0` to `15` | all exit 6: `LT_L3_COUNTER_INVALID`, **never initialised** (measured; the fake had assumed it) |
| `config restrict-shared --unit-slot 1` | refused (exit 9): "pairing slot 1 holds no usable key: restricting the shared key first would lock the chip" |
| **ECDSA on the real chip** | `ecc sign 0 --der` over a message; **OpenSSL on the PC verifies it** against the PEM public key from `ecc pub 0 --format pem`; that key is identical to the public key in the certificate in R-MEM slot 1 |
| EdDSA on a P-256 slot | exit 2 (refused, not a wrong answer) |
| legacy mode | `lt-util -x -f` exit 0; `lt-util -k -r 1` on the empty slot exit 1 |

### Correction: the bench chip's firmware version (2026-10-11)

The firmware version bytes `00 02 01 00` (RISC-V) and `00 01 03 00` (SPECT) were first read as "App FW 2.1.0 and SPECT FW
1.3.0" by matching them against release notes. That was wrong. libtropic's own example (`fw_update`) prints a version as
`byte 3 . byte 2 . byte 1`, which gives **RISC-V 0.1.2 and SPECT 0.3.1**. The same reading of the TROPIC01 software model's
bytes (`00 00 02 02`, `00 00 03 01`) gives 2.2.0 and 1.3.0, the versions libtropic 4.2 targets. So the bench chip runs
**firmware older than 2.0.0**: I-Memory writes (pairing slots, I-config) can fail silently there, which makes the read-back
after every such write essential, and it is far behind the 2.2.0 update files libtropic 4.2 ships. A firmware update is
itself a risky step and has not been considered. Consequences: `tropicctl config burn` (which requires 2.0.0 or newer)
refuses on this chip, as intended; the first version of that gate looked at the wrong byte and would have allowed it. That
is fixed and has a regression test. Any statement elsewhere that the bench chip has firmware 2.1.0 is wrong.

## Spare chip: firmware update and the commands that write (2026-10-10)

A second TROPIC01 (P/N TR01-C2P-T101, silicon ACAB, CHIP_ID v1.0.0.0), used only for tests that cannot be undone. All
commands are `tropicctl` built from the feed, run on the router over SSH.

| Check | Result |
|---|---|
| First look | RISC-V 0.3.1, SPECT 0.3.1, application mode, Maintenance Mode enabled in the I-config and the R-config. The shared engineering key (ES01) is **refused** (`LT_CRYPTO_ERR`): this chip has production provisioning, and **`--key prod0`** (the production slot-0 key published with libtropic) opens a session. Pairing: slot 0 populated, 1 to 3 empty. |
| Firmware update | `fw update --dir fw_v_2_2_0` (libtropic 4.2, `boot_v_2_0_1`): the chip reports **RISC-V 2.2.0, SPECT 1.3.0** afterwards; session, pairing list, random numbers and config reads work. |
| `fw maintenance off` | R-config START_UP `0xffffffff` to `0xfffffff7`, backup saved, read back, chip restarted. |
| ECC | generate P-256 and Ed25519, sign: **OpenSSL verifies both**; generating an occupied slot refused (exit 7); `ecc store` of P-256 and Ed25519 keys made with OpenSSL: **the chip's public keys equal OpenSSL's**; erase; sign with an erased slot: exit 6. |
| R-MEM | write, read back and `--replace` of 444 and 475 bytes (the larger slot of libtropic 4.2); occupied slot refused (exit 7); erase; read of an erased slot exit 6. |
| Pairing | `pairing write 1` with a per-unit X25519 key (**one way**): read back correctly, session with the new key opens, a second write is refused. |
| **R-config takes effect only after a restart** | `config restrict-shared --unit-slot 1`, `config rewrite` (21 objects, backup first) read back correctly. **Before `chip reboot` the shared key still read R-MEM**; after it, the shared key got exit 5 on R-MEM, config and ECC while the unit key worked. `config restore`, `chip reboot`: the shared key has its rights again. |
| MAC-and-Destroy | `mnd setup` (3 attempts), the right PIN releases the same key each time, a wrong PIN exit 8 ("2 attempt(s) left") and the right PIN still works after it, three wrong PINs leave no attempts and the right PIN is then refused (exit 9). |
| Counters | uninitialised: exit 6; init 3, three updates reach 0, a fourth is refused by the tool before the chip is asked; 4294967294 accepted; **init can be repeated** (counter 5 went from 0 to 9). |
| I-config burn | `config burn UAP_PING 31` on firmware 2.2.0: cleared and read back (I-config `0x7fffffff`). |

Not run: `--key pin` on a chip, `pairing invalidate`, the chip's own error for an update at zero (the tool refuses first),
host-state rollback of the PIN counter on a chip (the model confirmed it). State left on the spare chip: pairing slot 1
holds a test key, MAC-and-Destroy slots 0 to 2 are used, counters 5 and 6 are initialised, I-config UAP_PING bit 31 is 0.

Consequences for the project: the claim that counters "cannot be reset" was wrong and is corrected in cra-02; an access
rights change needs `chip reboot` before it protects anything; chips with production provisioning need `--key prod0`.

### Spare chip: PIN-protected pairing key and an access-right trap (2026-10-11)

| Check | Result |
|---|---|
| Setup | protected X25519 key written to pairing slot 2 (one way), data in R-MEM slot 50, PIN set with the unit key on MAC-and-Destroy slots 20 to 22, the key sealed under the PIN-released key |
| `--key pin` | opens a session with the protected key, before and after the R-config was restricted so that only that key may use R-MEM, ECC and counters |
| After the restart | the unit key and the shared key got exit 5 on R-MEM; `--key pin` with the right PIN read the data; a wrong PIN exit 8; the right PIN worked again afterwards and the attempts stood at 3 of 3 |
| **Trap found** | restoring the configuration with the unit key failed with `LT_L3_UNAUTHORIZED`: `config dump` also reads the I-config, which needs its own right (`UAP_I_CONFIG_READ`), and the restricted file had given that right to the protected key only. The unit key could still erase and rewrite the R-config, but could not read it first. |
| Fix | `chip_config_dump` now returns the R-config alone when the I-config may not be read (`config dump` and `config get` say so; `config burn` and `fw update`, which need I-config values, refuse with exit 5); the fake chip models the right. With the fixed build the unit key restored the saved configuration (19 objects, read back); after a chip restart the shared key read R-MEM again. |

Lesson: a restriction file that removes `UAP_I_CONFIG_READ` from the key that has to undo it can lock that key out of
the restore path (with the old build). Test the undo before relying on a restriction. State left on the spare chip:
pairing slots 1 and 2 hold test keys whose private halves were deleted, MAC-and-Destroy slots 20 to 22 are used.
