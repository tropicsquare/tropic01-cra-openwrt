# cra-06-security-logging — recording security-relevant activity

**CRA Annex I, Part I, 2(l). Status: partial (policy and configuration; no hardware involvement).**

> "provide security related information by recording and/or monitoring
> relevant internal activity, including the access to or modification of
> data, services or functions, with an opt-out mechanism for the user"

## At a glance

| | |
|---|---|
| Requirement | CRA Annex I, Part I, 2(l): record security-relevant activity, with a user opt-out |
| Status | partial: policy and configuration, no hardware involvement |
| Hardware needed | none |

## Role of TROPIC01

**None today.** Logging is software. A future role is to *anchor* the log: sign periodic hashes
of the log with a key held in the chip, or store the latest hash in R-MEM, so tampering with
older entries becomes detectable (backlog 6.3). Nothing like that exists yet, and the current log
can be altered by anyone with root.

## Hardware vs. policy

This requirement is **policy and software**. TROPIC01 contributes nothing to
it today, and nothing here pretends otherwise. A secure element could later
make the log *tamper-evident* (sign a hash chain of it with a key that never
leaves the chip); that is not implemented, see "What this does not fix".

## What is implemented

Package `tropic01-security-log`, enabled by default:

- **Security event log.** A small recorder follows the system log with a
  filter and appends each matching line to `/etc/tropic01/log/security.log`
  as it arrives (not buffered), rotating by size (default 256 KiB per file,
  the current file plus one rotated). The filter selects by program, because
  `logread` can match only the message text: SSH (`dropbear`), console login
  (`login`), LuCI (`dispatcher.uc`) and `rpcd` logins and failed logins,
  `passwd` changes, every program of this project (`tropic01-*`: console
  guard, audit, vault, pairing) and firmware-update messages. A program that is not on that
  list is not recorded.
- **Change audit.** `tropic01-config-audit` logs *which* security-relevant
  files changed (`/etc/config/*`, `/etc/shadow`, `/etc/passwd`, `/etc/group`,
  SSH `authorized_keys`, `/etc/tropic01/pairing.*`, `/etc/rc.local`, crontabs,
  `/etc/profile.d/*`, `/etc/init.d/*`, `/etc/sysupgrade.conf`) and which
  services were enabled or disabled. It records names only, never contents.
  A change made while it was not running (for example across a reboot) is
  caught against a baseline stored on flash and tagged `(while not running)`.
- **Opt-out and control** (the requirement asks for one):
  `tropic01-security-log status | show [N] | disable | enable | clear`.
  `disable` stops recording and monitoring and leaves stored lines;
  `clear` deletes them. Settings are in `/etc/config/tropic01-security-log`.
  Nothing is sent off the device by default; stock `system.log_ip` is the
  mechanism if a remote syslog server is wanted.

## What this does not fix

- **Not tamper-proof.** Root can edit or delete the log, and the log lives on
  the same flash. This is a record for diagnosis, not evidence. TROPIC01 could
  sign a hash chain of it (hardware-anchored tamper evidence); that is not built.
- **File-level, polled change detection.** There is no `inotify` on this
  image, so changes are found by comparing SHA-256 sums every `interval`
  seconds (default 30). A change reverted inside one interval is missed, and
  *who* made it is not recorded. Edits to files not on the watch list are not
  seen. No contents are logged, so *what* changed is also not known.
- **Not every activity is covered.** No network traffic or firewall-drop
  logging, no DHCP, no per-command audit, no alerting, no remote forwarding
  by default.
- **Timestamps are unreliable.** The board has no battery-backed clock;
  until NTP succeeds the date can be wrong (the image build date was seen).
  Audit lines therefore carry `uptime=`; the syslog date alone is not evidence.
- **Personal data.** Logs contain user names and peer IP addresses. That is why
  the opt-out and `clear` exist, and why the log directory is `0700`.
- **Flash wear** is bounded by the size cap and the low event rate; it is not
  zero.
- A **factory reset** erases the log with the rest of the overlay (good for
  privacy, bad for forensics). A sysupgrade that keeps settings keeps the log
  (`/etc/tropic01/` is on the keep list).

## Backlog

Open work (the TROPIC01-anchored tamper-evident log, trustworthy time, more event sources) is
tracked in the central [CRA backlog](../../docs/cra/backlog.md), section 6.

## Testing

Sandbox (`busybox sh`, fake `logread`): the change-audit loop (baseline, add,
change, remove, service enablement, offline changes, silence when idle, no
contents, stops on SIGTERM) and the recorder (history and live events, no
duplicates on restart, after a reboot, rotation, the start-up race, clean
shutdown).

On an OpenWrt One (`tests/hardware/bench.py cra06`, 18 checks, all pass): the
package payload was installed and started; real SSH and LuCI logins from a PC
over a wired link, failed and successful, were recorded; a config edit
(`/etc/config/system`) and a service enable/disable (`tropic01-aws`) were
recorded by file/service name; the size cap held; a 34 KB copy of the log
contained neither the real nor the wrong password; `disable` stopped both
processes and nothing new was recorded, the stored lines were kept, `enable`
resumed, and `clear` removed the log and baseline. This was run first with
the package from `/tmp`-pushed files, then on the flashed final image
(`bench.py suite`, cra-06 18 of 18).

Things the hardware testing showed about `logread` (they shaped the design):

- `logread -e` matches the message text only (the program tag is part of it;
  facility and level are not), and it is a **basic** regex: `a\|b` is
  alternation and `(a|b)` silently matches nothing. The filter therefore
  selects by program name.
- `logread -F` block-buffers its output file and `-S` does not cap it, so the
  recorder appends line by line and rotates by size itself.
- `logread -f` replays only the **last single line** of the buffer, and only
  if that line matches the filter, so the recorder takes its own snapshot at
  start-up; on a fresh start it records the whole buffered history that matches
  (a first start can write ~1000 lines). It must not wait for a particular line
  to reappear after the snapshot: an earlier version did, and a later hardware
  run showed it silently swallowing every event once the last buffered line did
  not match the filter. It now only drops leading lines that the snapshot's last
  few lines already cover. The sandbox gained the "follower replays nothing" case
  that reproduces this; the original 18-check hardware run had passed only because
  the last buffered line happened to match.

## Done means

- [x] events from SSH, console and LuCI logins are recorded
- [x] a config change and a service enablement are recorded, names only
- [x] the user can disable, inspect and erase the log
- [x] confirmed on hardware (see Testing)
- [x] this README says what is still not covered (above)
