# Hardware tests

The tests in `run.sh` use a fake chip. Before a command is relied on, it is also run on a real chip. Read-only
commands may be run on any unit; commands that write need a spare chip and a confirmation for every irreversible step.

## Read-only run (phase 1)

On the router with the chip (stop anything else that holds the chip first, for example `/etc/init.d/openvpn stop`):

```sh
tropicctl info chip
tropicctl info fw
tropicctl session test
tropicctl pairing list          # slot 0 populated, 1-3 empty on an unprovisioned unit
tropicctl config dump           # compare with docs/cra/hardware-test-report.md in the feed
tropicctl rng get 16
tropicctl mem list --range 0-10
tropicctl --json info fw
```

and, as `lt-util` (legacy mode), the same results as the old tool: `lt-util -i`, `-x -f`, `-x -r`, `-m -r 1 FILE`,
`-k -r 0 FILE`.

Record the result, the firmware version of the chip, and any difference from the fake in the project's hardware test
report. A difference means the fake is wrong and is fixed first.
