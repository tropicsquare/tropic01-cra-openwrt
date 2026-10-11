# cra-07 secure update: implementation guide for an AI agent

You are continuing the cra-07 feature (signed `sysupgrade` with the root key and rollback floor in TROPIC01). This file
tells you what exists, how it is designed, what to build next, in which order, how to test it, and what you must never
do. Read it completely before you change anything. The reasoning behind the design is in [design.md](design.md); this
file is the work plan.

---

## 0. Read first, in this order

1. `CLAUDE.md` (repository root) and, if present, `CLAUDE.local.md`: build rules, the **stop-and-ask** list, commit rules,
   and the **no commits or pushes 08:00 to 18:00 on working days** rule (check `date '+%a %H:%M'` first).
2. `features/cra-07-secure-update/CLAUDE.md`: this feature's own rules.
3. `features/cra-07-secure-update/README.md` and `docs/design.md`.
4. `features/core/tropicctl/README.md` and `features/core/tropicctl/docs/reference.md`: the chip tool everything here calls.
5. `docs/cra/backlog.md`, section 10: the open items of this feature (10.1 to 10.8). Update their status as you go.
6. `docs/cra/hardware-test-report.md`, the sections dated 2026-10-10 and 2026-10-11: what was **measured** on real chips.

## 1. Hard rules (non-negotiable)

| Rule | Why |
|---|---|
| Never run anything that writes a TROPIC01 on real hardware without explicit user confirmation **for that step, on that unit, in the current conversation**: R-MEM writes/erases, counter init, **every** counter update, pairing writes, R-config rewrites, I-config burns. | Several are one-way. A counter that is counted down can never go up. |
| Prepare chip-writing steps as a **script the user runs** (they run it with `! /path/script.sh` in the session) and read its log afterwards. The tool permission classifier blocks most remote chip writes from the agent. | Established practice in this project; do not try to work around a blocked call. |
| Stop OpenVPN before any chip command on the router and **verify by PID**: `/etc/init.d/openvpn stop; sleep 3; pidof openvpn` must print nothing. It restarts on every boot. | OpenVPN's PKCS#11 module holds the chip's only session; parallel access gives `LT_HAL_ERROR` or wrong results. |
| Never ask the spare chip for more than **24 random bytes in one call** (`tropicctl rng get N`, N <= 24). | On the updated spare chip (S/N ...0358, firmware 2.2.0) a request of 25 bytes or more puts the chip into alarm mode (needs a power cycle). Not to be written into the backlog (user decision). |
| Never commit or print private keys: `*.sec`, `key-build`, pairing keys, the router root password. Certificates (`*.ucert`) and public keys are not secret but are not committed either. Keys live outside the repository (the host tools refuse a path inside a git work tree). | Credentials. |
| Never edit `build/openwrt` (or any `$OPENWRT_DIR`) to fix something: it is regenerated. Changes to OpenWrt go into `openwrt-patches/NNNN-verb-description.patch` (`git format-patch`, minimal, a header that explains why). | Repository convention. |
| Do not push, open PRs or merge without the user's explicit say-so. Nothing is pushed before it ran on the real chip and router. | User rule. |
| Never claim TROPIC01 verifies signatures (it has no verify command) and never call this secure boot (that is cra-02). Keep the README's "what this does not fix" list accurate. | Project honesty rule. |
| Commits: Conventional Commits scoped to the feature, e.g. `feat(cra-07): ...`, author `kosmax871 <kosmax871@users.noreply.github.com>`, ending with the attribution line the session gives you. | Repository convention. |

## 2. Where things stand

- **Branch:** `feat/secure-update` in `/mnt/buildext/tmp/tropic01-public`, stacked on `docs/tropicctl-spec` (open PR to
  `main`; `tropicctl` is not on `main` yet). When that PR merges, rebase this branch onto the updated `main`
  (`git rebase --onto origin/main docs/tropicctl-spec feat/secure-update`) and re-run all tests.
- **Done (milestone 1, commit `ccc2e77`):** the router side, sandbox-tested.

| File | Role |
|---|---|
| `tropic01-secure-update/files/usr/sbin/tropic01-update` | the tool: `status`, `check IMAGE`, `commit`, `keys list`, `keys install SLOT FILE --yes-write-update-key [--replace]`, `floor init --yes-init-rollback-counter` |
| `tropic01-secure-update/files/lib/upgrade/zz-tropic01-secure-update.sh` | replaces `fwtool_check_signature` (sourced after `fwtool.sh` because files are sourced in name order); a refusal calls `notify_firmware_broken`, so `sysupgrade -F` cannot install the image |
| `tropic01-secure-update/files/etc/init.d/tropic01-update` | `START=86`: `tropic01-update commit` at boot, before OpenVPN (90) |
| `tropic01-secure-update/files/etc/config/tropic01-update` | UCI: `enforce` (1), `key_slots` ('200 201'), `counter` (0), `start` (1000) |
| `tropic01-secure-update/Makefile` | package, `DEPENDS:=+tropicctl +ucert +usign +fwtool` |
| `openwrt.config` | the package is **commented out** on purpose (an unprovisioned unit would refuse every update) |
| `tests/sandbox/secure-update.sh` | 58 checks; OpenWrt's real `usign`/`ucert`/`fwtool` + the real `tropicctl` on its fake chip |

- **Related, done elsewhere:** `/etc/tropic01/` is on the sysupgrade keep list (`features/core/tropicctl/files/keep.d`, on the
  cra-04 branch `feat/cra-04-pairing-provisioning`, commit `b706fca`, not on this branch). The pending-free commit design
  below does not need it, but provisioning files in `/etc/tropic01/` do.
- **Not started:** milestones 2 to 5 below.

## 3. Design in one page

```
 OPERATOR (offline)                 BUILD HOST                                  ROUTER (OpenWrt One + TROPIC01)
 root key   usign -G                                                            R-MEM slot 200 (201): root PUBLIC key (usign file, 86 B)
   │ ucert -I (issue)                                                           counter 0: c, floor = start - c
   ▼                                                                            
 key-build + key-build.ucert ──► $OPENWRT_DIR/key-build{,.ucert}
                                 TROPIC01_IMAGE_SV=N ──► image metadata "tropic01_sv": N (signed)
                                                     └─► rootfs /usr/share/tropic01/image-sv = N
                                 append-metadata: fwtool -I meta; usign -S; ucert -A; fwtool -S
                                                                  ──► sysupgrade.itb (signed) ──► sysupgrade / LuCI
                                                                                                      │
 validate_firmware_image ─► fwtool_check_signature (our hook) ─► tropic01-update check IMAGE ◄────────┘
   1 fwtool -s: has a ucert chain?          2 metadata has tropic01_sv?
   3 stop OpenVPN; tropicctl mem read 200/201 ─► /tmp/keys/<usign fingerprint>
   4 fwtool -T -s /dev/null IMG | ucert -V -m - -c chain -P /tmp/keys     (TROPIC01 does NOT verify)
   5 tropicctl counter get 0 ─► floor; refuse if sv < floor               any refusal ─► notify_firmware_broken
 boot S86: tropic01-update commit: sv(running) > floor ─► tropicctl counter update 0, exactly (sv - floor) times
```

Invariants the code must keep:

1. **Fail closed.** No key in the chip, chip unreachable, counter uninitialised, tool missing, metadata or signature missing:
   refuse. The only bypass is `enforce=0` (development) and it warns.
2. **A refusal is not forceable** (`notify_firmware_broken`).
3. **The floor only rises, and only by a booted image.** `commit` raises it to the running image's SV, never lowers it,
   and performs exactly `sv - floor` counter updates.
4. **`floor init` refuses an initialised counter** (re-initialising would lower the floor).
5. **TROPIC01 is storage and state.** Signatures are checked by `ucert` on the router.
6. **OpenVPN is stopped while the chip is used and started again**, on success and on failure (the `cleanup` trap).

Exit codes of `tropicctl` that the tool relies on: 0 ok, 3 unreachable/busy (retried up to 5 times), 5 denied, 6 empty
slot or uninitialised counter, 7 slot occupied, 9 refused by a safety gate, 10 chip error.

## 4. Pitfalls already met (do not repeat them)

- **POSIX sh has no local variables.** A helper that uses `i`, `n` or `rc` overwrites the caller's. This made `commit` loop
  until the counter hit 0 (caught by the sandbox test). Helpers use `_`-prefixed names; callers use distinct names. Any new
  loop that updates a counter must be bounded by a precomputed step count and tested for the exact end value.
- **`sysupgrade -F`** overrides "invalid"; only "broken" (`notify_firmware_broken`, which sets `FORCEABLE=0`) blocks it.
- **`IMAGE_METADATA` is reset per device** in `include/image.mk` (`Device/Init`), so a value set on the make command line does
  not reach `append-metadata`: it needs an OpenWrt patch (milestone 2).
- **`append-metadata` signs only if both `$(TOPDIR)/key-build` and `$(TOPDIR)/key-build.ucert` exist and are non-empty**
  (`include/image-commands.mk`). Otherwise the image is silently unsigned.
- **The metadata `version.revision` is OpenWrt's git revision**: two builds from the same OpenWrt pin share it. Do not use
  it to identify an image; use `tropic01_sv`.
- **`ucert` certificates carry a validity window** (issue time to one year later by default), and the router's clock is the
  image build date until NTP succeeds. An image signed with a certificate issued after the build date of the running image
  may be refused before NTP. Backlog 10.4: measure what `ucert -V` does with the window before deciding.
- **R-MEM rights are all-or-nothing per pairing key** (no per-slot scoping). The device key that may write the vault slot
  may also write slots 200/201. Do not claim the key slots are write-protected.
- **`counter init` can be repeated on the chip** (measured: 0 back to 9).
- **The sandbox test needs OpenWrt's host tools** (`$OPENWRT_DIR/staging_dir/host/bin`). Without them it prints `SKIP`
  (that is what CI does today). Run it locally with `OPENWRT_DIR=/mnt/buildext/tmp/tropic01-cra-openwrt/build/openwrt`.
- **A package that is not selected is not compiled** by `make package PKG=...` ("Nothing to be done"). To test the Makefile
  without putting it in the image, select it as a module in the generated tree only (`CONFIG_PACKAGE_...=m`, `make defconfig`).

## 5. Milestone 2: the build side (next)

Goal: `make build` with a signing directory and a security version produces an image that the milestone-1 check accepts,
and without them produces exactly what it produces today.

### 5.1 OpenWrt patch: the security version in the signed metadata

- New file `openwrt-patches/0003-image-add-tropic01-security-version-to-metadata.patch`, made with `git format-patch` in a
  clean OpenWrt checkout at the pinned commit (`openwrt.pin`), author `kosmax871`, header explaining why (rollback floor, see
  cra-07). Change in `include/image.mk`, `Device/Init`:
  `IMAGE_METADATA := $(if $(TROPIC01_IMAGE_SV),"tropic01_sv": $(TROPIC01_IMAGE_SV))`
  (the value ends up inside the single-quoted JSON of `metadata_json`; it must be a bare integer).
- Validate `TROPIC01_IMAGE_SV` in the harness (digits only, 1 to the counter `start`), never in the patch.
- Check that `scripts/prepare.sh` applies it and the `patches-apply` CI job passes (`git apply --check` against the pin).

### 5.2 The running image's security version in the root filesystem

- The harness writes `$OPENWRT_DIR/files/usr/share/tropic01/image-sv` (OpenWrt's `files/` overlay) from the same
  `TROPIC01_IMAGE_SV`, and removes it when the variable is unset, so an old value never leaks into a later build.
- `/usr/share/...` and not `/etc/tropic01/` on purpose: `/etc/tropic01/` is kept across upgrades, so an old value would
  shadow the new image's.

### 5.3 Harness: signing material

- Extend `scripts/prepare.sh` (or a new `scripts/signing.sh` called from the `build` target) with
  `TROPIC01_SIGNING_DIR=/path/outside/git`: copy `key-build` (mode 0600) and `key-build.ucert` into `$OPENWRT_DIR`. Refuse a
  path inside a git work tree (same check as `features/cra-01-secure-defaults/host-tools/tropic01-gen-credential.sh`).
  Unset: remove any `key-build*` the harness placed earlier (keep a marker file to know they were ours), so a build without
  signing is really unsigned and says so in its log.
- Export `TROPIC01_IMAGE_SV` into the OpenWrt `make` (the top-level `Makefile` passes variables through; check that it reaches
  `include/image.mk`).
- Document both variables in `make help`, `README.md` (build section) and `SKILLS.md`.

### 5.4 Host tool: root key and release certificates

`features/cra-07-secure-update/host-tools/tropic01-update-keys.sh`, POSIX sh, `set -eu`, `umask 077`, `log()`/`die()`,
modelled on `tropic01-gen-credential.sh`:

| Command | Does |
|---|---|
| `new-root DIR` | `usign -G` root key pair into DIR (outside git), prints the public key fingerprint only |
| `issue ROOTDIR SIGNDIR` | new release key pair `key-build` + `key-build.pub` in SIGNDIR, `ucert -I -c key-build.ucert -p key-build.pub -s ROOT.sec`; keeps the revoker `key-build.ucert.revoke` next to it |
| `show DIR` | fingerprints and the certificate dump (`ucert -D`), no secret material |

Never print a secret key; refuse a DIR inside a git work tree; refuse to overwrite existing keys.

### 5.5 Tests for milestone 2

- `tests/sandbox/secure-update-build.sh` (or an extension of `secure-update.sh`): run the host tool, then sign a fake image
  **exactly the way `append-metadata` does** (copy of its four commands), and check that `tropic01-update check` accepts it
  with the right SV; a certificate issued by another root is refused.
- A real `make build` with signing (long; run in the background, poll the log, report only what you saw finish):
  - the image log shows the signing commands ran; `fwtool -q -s x.ucert IMAGE` succeeds on the result;
  - `fwtool -q -i meta IMAGE` shows `"tropic01_sv": N`;
  - the rootfs contains `/usr/share/tropic01/image-sv` = N (unsquash the rootfs or check `build_dir/target-*/root-mediatek`);
  - a build **without** the variables is unsigned and has no `image-sv` (no leakage).
- `make lint`, `make test`, `scripts/check-doc-links.py`, the gitleaks scan: all clean.

## 6. Milestone 3: spare chip (needs the user, step by step)

Only on the **spare chip** (S/N 0x02001101085B1905090D000000000358, firmware 2.2.0, production slot-0 key: put `prod0` in the
key directory's `slot0-key` or pass `--key prod0`). Write one script per phase, the user runs it, you read the log. Each
phase lists its irreversible step.

1. **Image:** build an image with `tropic01-secure-update` selected and signing enabled (SV 1); flash only with the user's
   go-ahead (`sysupgrade` keeping settings; the router may need a power-and-USB unplug after a flash).
2. **Provision** (writes the chip): `tropic01-update keys install 200 root.pub --yes-write-update-key`, then
   `tropic01-update floor init --yes-init-rollback-counter`. **Irreversible:** the counter's range is fixed from now on.
   Use a counter not used before on that chip (counters 5 and 6 were used in earlier tests; 0 is free).
3. **Accept and refuse** (read-only for the chip): `sysupgrade -T` with: the signed SV 2 image (accept), an unsigned image,
   a tampered image, an image under another root, an SV 0 image after the floor rose (all refused, `-F` refused too).
4. **Commit:** flash the SV 2 image (user go-ahead), check after boot that the floor is 2 (`tropic01-update status`) and the
   counter went down by exactly 2 from its start. **Irreversible.**
5. Record everything in `docs/cra/hardware-test-report.md` (new dated section), update the README status, backlog 10.3.

## 7. Milestone 4: hardening (each item its own decision)

- **10.4 Validity and clock:** measure `ucert -V` with a certificate whose `validfrom` is in the router's future; choose long
  validity or a documented exception; add a test.
- **10.5 Revocation:** find out what `ucert -R` writes and where; keep revocations in a TROPIC01 slot if it fits; tests.
- **10.6 Who may write the key slots and initialise the counter:** design with cra-04 (`tropic01-pairing`); removing
  `MCOUNTER_INIT` from the device key in the R-config is reversible; burning it in I-config is **irreversible** and needs its
  own confirmation and a spare chip first.
- **10.8 CI:** build `usign`, `ucert`, `fwtool` (and their `libubox`, `json-c` dependencies) in the lint workflow so
  `secure-update.sh` runs in CI instead of skipping.
- Enable the package in `openwrt.config` only together with a documented provisioning flow, never before.

## 8. Milestone 5 (later)

- **10.7** The release key generated and kept inside a TROPIC01 on the build side (EdDSA signs the small `ucert` body, under
  the 4095-byte limit). First prove with the TROPIC01 software model (workspace `/mnt/buildext/tmp/tropicctl-model-tests/`,
  never committed) that a chip EdDSA signature in usign format verifies with `ucert -V`.

## 9. Definition of done for each milestone

- Code, tests and docs in the same commit series; `make lint`, `make test` (with `OPENWRT_DIR` set), doc links, gitleaks clean.
- New safety gates have a test that fails when the gate is removed (mutation check: edit a copy, point
  `TROPIC01_UPDATE_SCRIPT` at it, the test must fail).
- README "what this does not fix" still true; backlog section 10 updated; no "done" claim for anything only partly done.
- Hardware claims only for what you saw in a log; the hardware report says what was **not** run.

## 10. Report back

At the end of each milestone tell the user: what changed (files), what was tested and how (numbers), what was not tested,
what needs their decision or a chip step, and the commits (not pushed). Keep it short and plain.
