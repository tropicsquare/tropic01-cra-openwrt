---
name: build-image
description: >
  Build the OpenWrt One firmware image (or a single package) for this repo.
  Use when asked to build, rebuild, compile a package, or check that the
  tree builds. Covers the prepare step, long-running full builds, reading
  build failures, and where images and the SBOM end up.
---

# Build the OpenWrt One image

## Steps

1. Check prerequisites: either the OpenWrt build dependencies are
   installed, or Docker is available (then use `make docker-build`). Warn if
   the disk has less than ~40 GB free.
2. Run `make prepare`. It clones the pinned upstream OpenWrt into
   `build/openwrt`, applies `openwrt-patches/`, installs feeds and
   assembles `.config`. Report any patch that fails to apply: it means
   `openwrt.pin` and the patches disagree.
3. Run the full build **in the background**: `make build JOBS=$(nproc)`,
   output to a log file. A first build takes one to several hours. Do not
   report success until it has finished.
4. On success, list the images and the SBOM from
   `build/openwrt/bin/targets/mediatek/filogic/`.

For a single package: `make package PKG=<name>` (verbose output). Package
names are the folder names under `features/*/`.

## When the build fails

- Rerun only the failing package with `make package PKG=<name>` to get
  readable output, instead of rerunning the whole image with `-j1 V=s`.
- Fix the cause in this repo (package Makefile, patch, config fragment),
  never by editing `build/openwrt`.
- Check the "Known pitfalls" section of the `tropic01-pkcs11` skill, if
  present, before diagnosing TROPIC01 package errors from scratch.

## Do not

- flash the image to a router, or run provisioning, without the user
  asking for that specific step (see root CLAUDE.md "Safety")
- change `openwrt.pin` as a way to make a build pass without saying so
