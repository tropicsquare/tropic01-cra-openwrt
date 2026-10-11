# Instructions for AI assistants

This repo is an OpenWrt package feed + build harness that makes an OpenWrt
One router address EU CRA Annex I requirements with a TROPIC01 secure
element. Read [README.md](README.md) for the layout and
[docs/cra/annex-i-mapping.md](docs/cra/annex-i-mapping.md) for the scope of
each feature before changing anything.

Each `features/<name>/` folder has its own `CLAUDE.md` with rules for that
feature. Read it before working in that folder.

## Open work

One backlog for every CRA feature: [docs/cra/backlog.md](docs/cra/backlog.md). When a feature
README says something is "not done", it belongs there; keep the feature README to what the feature
does and does not do. Update the item's status when it moves.

## Building

- `make` builds the image; `make package PKG=<name>` builds one package.
- Never edit `build/openwrt` to fix something. It is regenerated from
  `openwrt.pin`, `openwrt-patches/`, `feeds.conf` and `features/`. Fix the
  source here.
- Changes to OpenWrt itself go in `openwrt-patches/` as numbered
  `git format-patch` files. Keep them minimal and explain why in the header.
- A first full build takes hours. Run it in the background and report the
  result; do not claim a build passed without seeing it finish.

## Safety: stop and ask

Stop and get explicit confirmation from the user, for that specific step,
before any of:

- anything irreversible or hazardous on hardware: writing or invalidating
  TROPIC01 pairing slots, erasing key slots or R-MEM, initialising or
  decrementing monotonic counters, clearing I-config bits, updating the chip
  firmware, rewriting the R-config, programming SoC eFuses
- flashing a router
- `git push`, creating or merging pull requests, publishing releases

Never commit or print private keys, pairing keys, per-unit passwords or
credential manifests.

## Honesty about scope

This project exists to show the difference between security by design and
security by checklist. Never describe a CRA requirement as "done" when only
part of it is addressed, and never credit TROPIC01 for something it does
not do (it is not the boot root of trust; it does not close ports). Every
feature README keeps a "what this does not fix" section. Keep it accurate.

## Conventions

- Packages: OpenWrt Makefile conventions; POSIX `sh` scripts with
  `logger`-based `log()` and a `die()` helper. Use
  `features/cra-01-secure-defaults/` as the template for new packages.
- Pin every source: `PKG_SOURCE_VERSION` is a commit hash or tag, never
  `HEAD`, with `PKG_MIRROR_HASH` set.
- Patches: `NNN-verb-description.patch`.
- Commits: Conventional Commits, scoped to the feature, e.g.
  `feat(cra-04): provision per-unit pairing key`.
