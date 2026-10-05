# Hardware bench tests

Host-side scripts that drive a real OpenWrt One over its USB debug console
(`/dev/ttyACM0`, 115200 8N1). They need `python3` and `pexpect`, nothing
else, and are **not** run in CI: CI has no board.

```sh
python3 tests/hardware/bench.py probe          # classify the console, send only a newline
python3 tests/hardware/bench.py cra01          # full cra-01 cycle (see below)
python3 tests/hardware/bench.py web            # listener / web checks from the board itself
python3 tests/hardware/bench.py cra04          # cra-04 checks on the chip with a patched lt-util pushed to /tmp
python3 tests/hardware/bench.py capture --settle 600   # record a boot, send nothing
```

## Rules the harness follows

- **One reader per port.** Two programs on `/dev/ttyACM0` split the bytes
  and corrupt each other. Close any serial terminal first.
- **It never types blind.** It waits for a recognisable prompt before
  sending anything; a keystroke at the wrong prompt becomes a password.
- **The bench root password is generated here**, kept in a `0600` file
  under `~/.cache/tropic01-bench/` (override with `BENCH_PW_FILE`), sent
  without being logged and never printed. It is a throwaway credential for
  a bench unit. Transcripts go in the same directory, outside the repo.
- Nothing here touches TROPIC01 slots or any irreversible state.

## What `cra01` does

Logs in, plants the sha256-crypt hash of the empty string for root (the
state LuCI's first-login screen used to create), reboots, waits for the
boot-time guard, and then checks, unattended:

1. the boot-time guard cleared the empty password before any login
2. an empty new password is refused on every attempt, and the session
   closes after five failures
3. a real password is then accepted
4. `tropic01-console-guard state` is `set`, `ttylogin` is 1, and the
   first-boot warnings are gone from the MOTD and the dropbear banner
5. knocking `ttylogin` back to 0 is repaired by the guard

`cra05 --phase stack` runs the vault's whole encryption stack on the board with a
throwaway RAM image and a random key (no chip access); the chip steps (`init`,
`destroy`) are done by hand because they write and erase R-MEM.

`web` checks, from the board itself over loopback (the host cannot reach the
router's LAN): SSH bound to the LAN address only, HTTP/HTTPS answering, rpcd
rejecting an empty password.

`cra04` pushes the patched `lt-util` to `/tmp` over the wired bench link and, with
nothing flashed and nothing written to the chip, checks: the shared key still works
with no key files; the four pairing slots can be inspected (read-only); unusable key
files are refused without falling back; a wrong but well-formed key is rejected by the
chip; and removing the files restores access.

## Wired bench link (recommended)

The shared Wi-Fi LAN made board-to-PC traffic unreliable. A dedicated cable avoids that
and also allows real SSH/LuCI tests from the PC:

1. Cable from a spare PC NIC to the router's **LAN** port (the 1G port).
2. A NetworkManager profile with a static address and no default route (no root needed
   if `nmcli general permissions` shows `settings.modify.system: yes`):
   `nmcli connection add type ethernet ifname <nic> con-name tropic01-bench ipv4.method manual ipv4.addresses 192.168.9.2/24 ipv4.never-default yes ipv6.method disabled`
3. On the router, move its LAN off the default 192.168.1.0/24 (which clashes with most
   home networks): `uci set network.lan.ipaddr=192.168.9.1/24 && uci commit network &&
   /etc/init.d/network reload`. Keep the `/24`: a bare address gives a `/32`.
   A clean flash resets it.

## Known hazards

- **Do not push data through the serial console.** Sending a few KB of base64 into the
  console hung the board both times it was tried (console and network dead, only the
  BootROM lines on the next open). Use the wired link and `nc`.
- **The board has hung several times** (after a `reboot`, and during that transfer) and
  then stayed dead until power *and* the USB debug cable were fully unplugged. The cause
  is not known; a different USB-C supply and cable are the first thing to try.

## What it cannot do

- **Power-cycle the board.** A hung boot needs a hand on the cable. A
  switchable plug or relay on the supply would remove that.
- **Flash.** Use failsafe (`f` + Enter at boot) and `sysupgrade`, or the
  U-Boot TFTP menu.
- **Test from the LAN side.** SSH/LuCI logins from another machine need a
  host NIC on the router's LAN subnet (a USB-Ethernet adapter in a network
  namespace, which needs root once).
