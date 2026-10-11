# Working on this project with an AI agent

This repository is set up for AI coding agents (Claude Code and similar). Agents get
their instructions from three places:

| File | What it gives the agent |
|---|---|
| [CLAUDE.md](CLAUDE.md) | Project rules: build commands, the "stop and ask" safety list, honesty rules about CRA scope, conventions |
| `features/<name>/CLAUDE.md` | Rules specific to one feature (read before touching that folder) |
| [.claude/skills/](.claude/skills/) | Task playbooks ("skills") the agent loads when your request matches |

Other agents can read the same files: point them at `CLAUDE.md` and the skill files
and ask them to follow them.

## Available skills

| Skill | Use it for | File |
|---|---|---|
| `build-image` | Building the firmware image or a single package, reading build failures, finding the outputs and SBOM | [.claude/skills/build-image/SKILL.md](.claude/skills/build-image/SKILL.md) |
| `tropic01-pkcs11` | TROPIC01 integration: `tropicctl`, the PKCS#11 module, slot layout, key and certificate provisioning, known pitfalls | [.claude/skills/tropic01-pkcs11/SKILL.md](.claude/skills/tropic01-pkcs11/SKILL.md) |

## Rules every agent must follow

These are enforced by instruction, not by the tooling. Check what the agent proposes.

The agent must **stop and ask you** before:

- anything irreversible or hazardous on the chip: writing or invalidating pairing key slots,
  erasing key slots or R-MEM, initialising or decrementing monotonic counters, clearing
  I-config bits, updating the chip firmware, rewriting the R-config, generating a key or
  storing a certificate in a slot
- flashing a router
- `git push`, creating or merging a pull request, publishing a release

The agent must never commit or print private keys, pairing keys, per-unit passwords,
credential manifests or filled-in `.ovpn` profiles, and must never describe a CRA
requirement as "done" when it is only partly addressed.

A first full build takes hours. The agent should run it in the background and report
the result only after it finishes.

## Example prompts

Copy, adjust and paste. Each one is safe to give as written: none authorises a
hardware or git action beyond what it says.

### Build

> Build the firmware image for the OpenWrt One. Use Docker if the build dependencies
> are not installed. Run it in the background, and tell me where the images and the
> SBOM are when it finishes.

> `make package PKG=tropic01-secure-defaults` fails. Find the cause and fix it in the
> repository (not in `build/openwrt`). Show me the diff.

> Check that a clean checkout builds: clone into a scratch directory, run
> `make prepare` and report whether every patch in `openwrt-patches/` applies.

### Check without hardware

> Run `make lint` and `make test`, then `python3 scripts/check-doc-links.py`. Summarise
> failures only.

> I changed the vault script. Run the sandbox tests and tell me which of the checks
> in `tests/sandbox/` cover the change. Add a check if none does.

### Flash and test on hardware

> I have an OpenWrt One in failsafe mode on the serial port. Read
> `tests/hardware/README.md` and tell me the exact commands to flash the image and
> run the bench suite. **Do not run them.** I will confirm each step.

> Flash `<path to sysupgrade image>` with `sysupgrade -n`. I confirm: flashing this
> router now is intended. After the reboot, wait for it to come back and run the
> `probe` and `cra01` bench checks.

### TROPIC01 and OpenVPN

> Explain how to create a client key in TROPIC01 slot 1, make a CSR from it and install
> an OpenVPN profile, following `features/cra-03-key-storage/docs/openvpn-user-guide.md`.
> List which steps write the chip. Do not run anything.

> Install my profile `<file.ovpn>` on the router at `<ip>` with
> `tropic01-install-ovpn.sh`, using the certificate already in slot 1. Do not commit
> or print any part of the profile.

### Features and documentation

> Read `docs/cra/annex-i-mapping.md` and the cra-05 README, then tell me what the
> encrypted vault protects, what it does not, and what TROPIC01 contributes.

> Add a new item to `docs/cra/backlog.md` for `<idea>`, in the right section, in the
> same format as the existing items.

> I am adding a new package. Use `features/cra-01-secure-defaults/` as the template,
> pin the source, add a sandbox test if it has shell logic, and write the "what this
> does not fix" section honestly.

### Prepare a contribution

> Review my changes on this branch against CONTRIBUTING.md and the rules in CLAUDE.md.
> Report problems; do not push or open a pull request.

## Adding or changing a skill

A skill is a folder `.claude/skills/<name>/` with a `SKILL.md`. The file starts with
front matter (`name`, `description`); the description is what the agent uses to decide
when to load it, so state the situations that trigger it. Keep the steps short, link to
the authoritative docs instead of copying them, and state what the agent must not do.
After editing, run `python3 scripts/check-doc-links.py`.
