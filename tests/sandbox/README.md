# Sandbox tests

Run the router-side shell scripts on an ordinary Linux host, with fake `logread`, `tropicctl`, `cryptsetup`,
`mount` and `uci`, so their **logic and safety rules** can be checked without a board:

```sh
make test          # or: tests/sandbox/run-all.sh
```

| Test | Script under test | What it checks |
|---|---|---|
| `audit.sh` | `tropic01-config-audit` (cra-06) | baseline, added/changed/removed files, service enablement, changes made while it was not running, silence when idle, no file contents in the log, stops on `SIGTERM` |
| `recorder.sh` | `tropic01-log-recorder` (cra-06) | history and live events, no duplicates on restart, a rebooted buffer, rotation by size, clean shutdown, the start-up race, a follower that replays nothing. Needs `busybox` (`apt install busybox`); skipped without it |
| `vault.sh` | `tropic01-vault` (cra-05) | init/open/close/destroy state machine, a key is never left on disk, a non-empty slot is never overwritten, `destroy` needs its flag and keeps the image if the erase fails, only slot 100 is ever touched |

These tests **do not** exercise the hardware, the real encryption, or the real `logread`: those are covered by
[`tests/hardware`](../hardware/README.md), which needs a board. A test here that passes only shows the script
behaves as designed against the fakes; each fake is deliberately modelled on behaviour seen on a real unit
(for example, `logread -f` replaying only the last line).

Each script prints `PASS`/`FAIL` per check and exits non-zero on any failure. Shared helpers are in `lib.sh`.
