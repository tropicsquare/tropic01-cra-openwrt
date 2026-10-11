# Hardware tests

The tests in `run.sh` use a fake chip. Before a command is relied on, it is also run on a real chip. Read-only
commands may be run on any unit; commands that write need a spare chip and a confirmation for every irreversible step.

## Read-only run

On the router with the chip (stop anything else that holds the chip first, for example `/etc/init.d/openvpn stop`):

```sh
tropicctl info chip
tropicctl info fw
tropicctl session test
tropicctl pairing list          # slot 0 populated, 1-3 empty on an unprovisioned unit
tropicctl config dump           # compare with docs/cra/hardware-test-report.md in the feed
tropicctl rng get 16
tropicctl mem list --range 0-10
tropicctl ecc list              # 32 lines
tropicctl config dump --decode
tropicctl info mode
tropicctl info cert
tropicctl --json info fw
```

and, as `lt-util` (legacy mode), the same results as the old tool: `lt-util -i`, `-x -f`, `-x -r`, `-m -r 1 FILE`,
`-k -r 0 FILE`.

Record the result, the firmware version of the chip, and any difference from the fake in the project's hardware test
report. A difference means the fake is wrong and is fixed first.

## Commands that write

These need a spare chip, because a pairing slot, an I-config bit and an R-config rewrite cannot be undone on a unit that is
in use. Run them in this order, reading the output after each, and record the results in the hardware report:

1. `ecc generate`, `ecc pub`, `ecc sign` on an empty ECC slot; verify the signature on a PC with OpenSSL; `ecc erase`.
2. `mem write`, `mem read`, `mem erase` on a free slot (the vault already exercises these on slot 100).
3. `pairing write 1` with a per-unit key; `session test --key unit`; `config restrict-shared --unit-slot 1`; `config diff`
   and `config rewrite` (with a backup); check the shared key is refused and the per-unit key still works; `config restore`.
4. `mnd setup`, `mnd check` with a right and a wrong PIN, exhaust the attempts; the PIN-protected session (`--key pin`).
5. `counter init`, `counter update` to zero, and an update at zero; confirm the direction and the limit.
6. Only then, and only with explicit confirmation for that chip, anything that cannot be undone in the end state
   (`pairing invalidate`, `config burn`).

Differences between the chip and the fake are fixed in the fake first.

## Firmware update

Run on a spare chip only; an update cannot be undone (a chip never accepts an older version).

1. `tropicctl fw status` and `tropicctl config get START_UP`: the images must match the silicon revision (this build:
   `fw status` prints it), and Maintenance Mode (bit 3) must be set in the I-config.
2. A chip with production provisioning refuses the engineering key: use `--key prod0` for every command.
3. `tropicctl --key prod0 --dry-run fw update --dir DIR --yes-update-firmware`, then without `--dry-run`. DIR holds one
   `fw_v*_signed_chunks.bin` and one `spect_app-v*_signed_chunks.bin` from libtropic's `TROPIC01_fw_update_files/`
   (`boot_v_2_0_1` for silicon ACAB, `boot_v_1_0_1` for ABAB). Run it from a stable power supply, in the background if
   it goes over SSH, and read the log.
4. `tropicctl fw status`, then `tropicctl fw maintenance off --yes-rewrite-r-config` and a `chip reboot`.
5. After any R-config rewrite, check the change is not in force until `tropicctl chip reboot` (the fake and the model
   both assume this; record what the chip does).
