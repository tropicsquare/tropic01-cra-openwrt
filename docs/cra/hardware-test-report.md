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

Not re-run after the upgrade: the vault `init`/`destroy` (they write and erase R-MEM slot 100), an OpenVPN tunnel
(the bench has no WAN link) and the AWS test. The chip's firmware version has not been compared with what 4.2 ships.

### Clean-container build (2026-10-10)

The image was then rebuilt from a **fresh clone of the pushed branch inside the Docker builder image, with no
download cache**, and flashed clean (this time the board came back without a stall). SHA-256
`b87a1c459171065ea6d7937fcc82e729c12e2ffd10b79ceebad7ef2a8588b550`.

- `bench.py suite`: **56 of 56** again, and a CSR made with the chip's slot 0 key through the PKCS#11 module verifies
  on a PC.
- The clean build found two build-environment problems that cached downloads and a fuller host had hidden, both fixed:
  the pinned source hash of `libtropic-pkcs11` (and `lt-util`) did not reproduce on a fresh download, and the Debian
  builder image lacked the `ip` command that OpenVPN's `configure` needs (`iproute2` added).
