# CRA backlog

Purpose: one list for everything still open across the CRA features, for contributors and
for anyone judging how complete the project is. Nothing here is a promise or a schedule;
items are ordered by dependency and value, not by date.

One list for everything still open across the CRA features. Each feature README keeps what
the feature **does and does not do**; what is left to do lives here, so there is one place
to steer from. Update an item's status here when it moves, and when a feature README says
something is "not done", the item should exist below.

How to read an item: **what**, **why** (the Annex I point or the risk it closes),
**needs** (dependencies), **size** (S = hours, M = days, L = weeks), and **irreversible?**
where a step burns something on hardware (key slots, eFuses, counters). Anything marked
irreversible needs an explicit, per-unit go-ahead from the person running it, every
time (see the feature `CLAUDE.md` files). To add an item, put it in the section of the
feature it belongs to and follow the same format.

Status of the features themselves: [annex-i-mapping.md](annex-i-mapping.md).

## Suggested order

1. **Merge readiness** (section 1): re-test what the base-image changes may have broken,
   rebuild the image with the latest fixes, then PR with CI.
2. **cra-04** (4): provisioning works (spare chip and bench unit); open are slot 0
   invalidation, the private key's home and access-right scoping.
3. **cra-02 verified boot** (2): the largest item, and cra-01's reset integrity waits on it.
4. Everything else by value: the first-password web page and the management UI (6.1, 9.1), log
   anchoring, storage encryption.
5. **cra-03: AWS telemetry** (3.1), deliberately last: it needs AWS account settings.

```
cra-02 verified boot ──► cra-01 reset integrity
cra-04 per-unit pairing key + slot 0 invalidated ──► cra-05 stronger vault / PIN storage
                                                  └─► cra-06 log anchoring in R-MEM
cra-05 encrypted storage ──► cra-04 private key placement, cra-06 log in the vault
```

## 1. Merge readiness and project hygiene

- **1.1 Final image with the latest fixes.** *Done 2026-10-05:* image `bf871831...` built from the committed
  tree, flashed clean, and `bench.py suite` passed 56 of 56 checks (web, cra-04, cra-05 stack, cra-06
  from the installed package, cra-01 boot-time guard cycle), plus the vault `init` and `destroy` and
  OpenVPN by hand. Re-run after any further change to what ships in the image. See
  [hardware-test-report.md](hardware-test-report.md).
- **1.2 PR with CI.** `make` from a clean checkout, `make lint`, the secret scan, and the
  PR description with how and on which hardware revision it was tested
  ([CONTRIBUTING.md](../../CONTRIBUTING.md)). CI has not been run on this branch. The branch is
  large (core, cra-01, 03, 04, 05, 06); decide whether to split it. *S-M.*
- **1.3 LuCI default for `main`: decided, keep it in the default image** (2026-10-05). It is extra LAN-side
  attack surface and enforces no password, which the cra-01 README says; the first-login gate
  (6.1) is what closes it. *Done.*
- **1.4 A field report with a wrong claim.** *Done:* the old hardware findings note (which said a keep-settings flash
  preserves the overlay, wrong on this board, and held a device address and an AWS endpoint) was replaced by
  [`aws-iot-known-issues.md`](../../features/cra-03-key-storage/docs/aws-iot-known-issues.md).
- **1.5 Hook closing message.** The forced-password hook now sleeps 2 s before closing a
  session so its "closing this session" line is not cut off. Confirm on hardware. *S.*
- **1.6 Why `firstboot -y && reboot` did not wipe the overlay** on this board (a clean
  first boot was only produced with `sysupgrade -n`). Not investigated. *S.*

## 2. cra-02 verified boot and rollback (not started)

Annex I 2(f), 2(c). Design is in [cra-02's README](../../features/cra-02-verified-boot/README.md).

- **2.1 Secure boot chain** (BL2 to FIP to U-Boot, FIT image signature). The root of trust
  is the SoC boot ROM and its fused key, not TROPIC01. **Irreversible:** eFuse
  programming. Test with non-production keys on a spare board first. *L.*
- **2.2 Signed sysupgrade** and **2.3 anti-rollback** moved to their own feature, cra-07-secure-update (section 10).
  Secure boot (2.1) stays here; first find out whether this board's boot ROM can verify a signature at all.
- **2.4 Signed recovery image in the write-protected SPI-NOR** as the base for verified
  reset. *M.*

- **2.5 cra-01 reset integrity.** A factory reset should restore a *verified* baseline: check
  a signature over it against the same key as secure boot before applying it. Today a reset
  re-runs `firstboot` and restores whatever the squashfs contains, unverified. Do not build a
  "verified reset" on an unsigned boot chain; it would verify against nothing. Needs 2.1 and
  2.4. *M.*

## 3. Platform, TROPIC01 integration and cra-03

- **3.1 AWS IoT telemetry: moved out of cra-03's claim (2026-10-05); fix it here.** OpenVPN with the key in TROPIC01 was
  re-tested on the final image on 2026-10-05: tunnel up, pings through it, and it comes up by
  itself after a reboot (see the cra-03 README). AWS IoT telemetry still fails and did before this
  branch: the OpenVPN/AWS SPI hand-off uses a fixed `sleep 2` and should poll for the chip, and
  the MQTT connect is rejected by AWS (certificate not ACTIVE or no matching IoT policy: check the
  AWS side). The AWS config was wiped by the clean flashes and has to be re-entered. *M.*
- **3.2 PKCS#11 runtime path with a per-unit pairing key.** `libtropic-pkcs11` has the
  loader but no consumer exercised it on hardware. Needs a PKCS#11 consumer on the unit
  (`pkcs11-test`, or the VPN). *S.*
- **3.3 Three copies of libtropic.** *Done (branch `feat/libtropic-4.2`):* one pinned libtropic v4.2.0 package; `tropicctl` and
  `libtropic-pkcs11` build against its source tree (OpenSSL crypto layer, Linux SPI hardware layer).
- **3.4 Bump OpenVPN** (pinned at 2.6.12) after a hardware test. *S-M.*
- **3.5 MAC-and-Destroy in the command-line tool.** *Done in `tropicctl` (`mnd setup|check|status|seal|unseal`, on libtropic's
  `lt_mac_and_destroy()`), tested against the fake chip, the TROPIC01 software model and a spare chip (2026-10-10: setup, right and wrong PIN, attempts exhausted).* Needed by 5.4. The PIN-protected
  session (`--key pin`) was also run on a spare chip. Open: the multiple-PIN and wiping-PIN extensions of the application note. *S.*
- **3.6 `opensc` does not build** against OpenWrt's fortify headers; `pkcs11-test` covers the
  need. Low priority. *-.*

- **3.9 Polish the OpenVPN enrolment path.** The user guide
  ([openvpn-user-guide.md](../../features/cra-03-key-storage/docs/openvpn-user-guide.md)) is verified on hardware end
  to end (key in a spare slot, CSR signed by the chip, CA step, certificate in R-MEM, key and certificate belong
  together, clean-up). Open: test the CA step with easy-rsa (it adds extensions that may exceed 444 bytes, the most the PKCS#11 module reads from a slot),
  check PKCS#11 ids for slots above 9, ship the `openssl-pkcs11.cnf` and a CSR helper in the image instead of a
  copy-paste block, and connect with an identity enrolled this way against a real CA. `openssl-util` is in cra-03's config
  for the next image build; the final test image does not contain it. *S-M.*
- **3.10 Debug output that must not ship.** *Command-line part done:* libtropic is built with `LT_LOG_LVL=None` and
  without raw SPI data printing. Still open: `libtropic-pkcs11` writes every call (including key handles) to syslog at debug level
  (`006-add-syslog-tracing.patch`). Both are useful while bringing up hardware, and both are unwanted on a unit whose
  `logread` anyone on the network can read. Make them build-time options, off in release images. *S.*
- **3.7 Upgrade libtropic.** *Done to v4.2.0 (not 4.1: 4.2 raises the R-MEM read buffer from 444 to 475 bytes).*
  Verified on an OpenWrt One: chip identification, the secure session with the shared key, R-MEM read,
  pairing-slot inspection and the per-unit-key refusal rules (`bench.py cra04`, 13 of 13), and a certificate
  request signed by the chip through `libtropic-pkcs11` that verifies on a PC. *Final image (2026-10-09, rebuilt from a clean
  container clone 2026-10-10):* flashed clean, `bench.py suite` 56 of 56 both times. Vault `init`/`destroy` and an OpenVPN tunnel were also run on it
  (see the hardware report). Still to run: the AWS test (3.1).
  Open: the bench chip runs RISC-V firmware **0.1.2** and SPECT **0.3.1** (older than 2.0.0; 4.2 ships 2.2.0 update files; see the hardware report); decide whether to update the bench chip too
  (`tropicctl fw update` updated the spare chip to 2.2.0 / 1.3.0). The bench chip is silicon ABAB and the build's update
  images are for ACAB (`LT_SILICON_REV`), so `fw update` refuses it as built. *S.*
- **3.13 A new, documented command-line tool for TROPIC01 (`tropicctl`), replacing the patched `lt-util`.** Specification:
  [`features/core/tropicctl/SPEC.md`](../../features/core/tropicctl/SPEC.md). An OpenWrt package in this feed, written so
  it can move to its own repository; hand-written fake chip plus hardware tests. *Phases 1 to 4 implemented
  (2026-10-11): `info`, `rng`, `mem`, `ecc`, `pairing`, `config`, `mnd`, `counter`, `session`, `chip reboot`,
  `fw status/update/maintenance`, PIN-protected sessions,
  legacy mode for every old `lt-util` command, about 370 tests, every irreversible and hazardous guard checked by
  mutation; callers (vault, AWS provisioning script, bench, docs) migrated; read-only run on the bench chip.* The commands that write
  were run on a spare chip (hardware report). Open: (a) `pairing invalidate` on a spare chip, (b) the console-driven
  `bench.py suite` on a flashed image, (c) a release process if the tool moves to its own repository. *M.*
- **3.14 R-MEM slot 0 reads back identical to slot 1 on the bench chip** (found by the tropicctl read-only run,
  2026-10-10), while the documentation says slot 0 is read-protected on the ES silicon. Find out whether slot 0 was
  also written at some point or whether reads of slot 0 are aliased to slot 1, (the documentation now says
  "slot 0 is not used" and keeps certificates in slot N+1).
  A spare chip: write a distinct value to slot 1, read slot 0. *S.*
- **3.8 `lt-util` after the libtropic upgrade.** *Superseded by `tropicctl` (3.13): the patched `lt-util` and its six patches
  are gone from the feed; the package installs an `lt-util` symlink that runs the new tool in compatibility mode.*

## 4. cra-04 access control (partial)

Annex I 2(d). Software side is done (per-unit key generator, key-file support, `tropicctl pairing`, and the guarded
provisioning command `tropic01-pairing` with sandbox tests); the bench unit and a spare chip are provisioned (per-unit key, shared key restricted); **slot 0 stays
valid** by decision.

- **4.1 Provision a per-unit pairing key** into slot 1 and read it back. *`tropic01-pairing provision` does it with gates and
  a verified restriction of the shared key; run end to end on a spare chip on 2026-10-11 (slot 3, restriction verified, undo works); the bench unit also has a
  per-unit key and a restricted shared key (2026-10-11). Open: other units.* **Irreversible per
  unit** (a written slot cannot be rewritten; the write goes to I-Memory, which can fail
  silently on old firmware). Use a spare chip first. *S per unit.*
- **4.2 Invalidate slot 0** (the shared engineering-sample key), only after 4.1 is verified
  in use. **Irreversible and can lock the chip for good.** Not before explicit confirmation
  on that unit. *S per unit.*
- **4.3 UAP scoping per key** (for example a telemetry key that can sign but has no R-MEM rights). The shared key's
  restriction is done by `tropic01-pairing`. R-MEM rights are all-or-nothing per pairing key, so a single
  write-restricted R-MEM slot is not possible. Test on a spare chip first. *M.*
- **4.4 Where the host's private pairing key lives.** *A PIN-protected pairing key (tier 1) is implemented in `tropicctl`
  (`--key pin`, `mnd seal`) and was run on a spare chip (hardware report, 2026-10-11); the design is in the cra-04 design draft.* Today a root-only file on flash, so
  root or a flash dump of one unit can open that unit's chip. A stronger home needs encrypted
  storage (5.3, 5.4) or a PIN (5.4). Note the chicken-and-egg: the key must be readable
  before the overlay is unlocked. *M.*
- **4.5 Per-unit image versus one signed fleet image.** Baking per-unit secrets into an
  image means one build per unit, in tension with cra-02's single signed image, and a factory
  reset after a fleet sysupgrade restores the stock *blank* root password. Planned fix:
  keep the per-unit credential (password hash or SSH public key) in a TROPIC01 R-MEM slot and
  have a first-boot script apply it from one fleet image. Needs 4.1. *M.*
- **4.6 Process gates.** Nothing checks that a production unit ran the credential generator
  or skipped the dev test key; a dev image (`/etc/tropic01-dev-build`) can technically
  ship. A release check is a process or CI rule. *S.*

## 5. cra-05 encrypted storage and erase (partial)

Annex I 2(m). The vault works on hardware (init, use, lock, reboot auto-unlock, fail-closed,
destroy). Only `/vault` is encrypted.

- **5.1 Stricter crypto-erase demonstration.** *Done 2026-10-05:* a copy of the encrypted image was taken
  off the board before `destroy`; after the erase the old copy was put back and the device refused to
  open it (`could not read the vault key from TROPIC01 slot 100`).
- **5.2 Decommission and reset hook.** A factory reset or decommission that calls
  `tropic01-vault destroy` and then wipes the overlay. Today a reset leaves the vault
  to the user. *S-M.*
- **5.3 Put more in the vault.** Move the cra-06 security log (user names and IP addresses)
  into it, and decide what else is "owner data". Resizing, and a dedicated UBI volume so a
  factory reset can leave it alone. *M.*
- **5.4 Full writable-storage encryption unlocked by a user PIN (TROPIC01 MAC-and-Destroy).**
  Extend the vault idea from one folder to the whole writable overlay (every setting, key
  and file), with the key released only by a PIN checked inside the chip.
  - *Mechanism.* libtropic's `lt_PIN_set` / `lt_PIN_check` (and `tropicctl mnd setup` /
    `mnd check`, see 3.5) derive a 32-byte secret from a PIN of 4-8
    characters. Every wrong guess destroys one of the chip's 128 MAC-and-Destroy slots, and
    tropicctl's default is 5 attempts, so the PIN cannot be brute-forced in bulk. That
    secret would be the key-encryption key that unwraps the LUKS volume key, so a stolen unit
    cannot unlock itself without the PIN, even while the shared pairing key is still valid
    (it cannot skip the attempt limit).
  - *Scope is the overlay, not the squashfs.* The read-only squashfs inside the FIT image
    cannot be encrypted without U-Boot support; the writable overlay (a LUKS2 loop image
    under the overlay, or fscrypt, which needs a kernel change) is the realistic target.
    The read-only root, the TROPIC01 tools and the pairing key must stay readable before
    unlock.
  - *Boot and PIN entry on a headless router is the hard part:* where does the user type the
    PIN (serial console, an SSH/dropbear stage in early boot, a pre-login web page)? An
    unattended reboot cannot unlock without weakening the scheme. Fail closed to a RAM-only
    overlay when not unlocked and keep failsafe reachable.
  - *Attempt exhaustion is permanent data loss,* and anyone with access to the SPI bus can
    burn the attempts, so this is a lockout and denial-of-service risk as well as a
    protection. A recovery story (a reset that discards the data, or an escrowed recovery
    key) must be decided first.
  - *Host-side state:* the helper's counter and ciphertexts (`lt_macandd_nvm_t`, about 400
    bytes) must survive power cycles and be readable before unlock, so they live outside the
    encrypted area.
  - *Depends on* 4.1/4.2 for the strongest claim, and on the datasheet for what
    MAC-and-Destroy does and does not guarantee before any wording goes in a README.
  *L.*

## 6. cra-06 logging and cra-01 first-login

- **6.1 Web page for the first password (LuCI first-login gate, cra-01 Part C).** Requested again
  2026-10-05 as a web page for setting the first password; it is the same item, and 9.1 builds on
  it. A small LAN-only setup page served while root is blank, with real LuCI held back until a
  password exists. LuCI itself only nags and
  has no hook this repo can patch; today the first person on the LAN can log in to LuCI
  with a blank password and choose the root password. Needs an HTTPS decision (`luci-ssl`'s
  self-signed certificate vs plain HTTP on the LAN) and a prototype. *M.*
- **6.2 LuCI gives no feedback** when a blank new password is saved (the busybox patch
  refuses it, nothing is shown). Cosmetic and upstream. *-.*
- **6.3 Tamper-evident log anchored in TROPIC01.** Keep a SHA-256 hash chain over the stored
  log (each chunk's hash covers the previous one), sign the chain head with a TROPIC01 ECC
  key (`tropicctl ecc sign` / PKCS#11), and store the head hash and signature in an R-MEM slot
  (475 bytes, enough for both). `show` could then verify the stored log against the chip.
  - It detects an edit, truncation or deletion **after the last anchor**; it cannot detect
    events that were never logged, or tampering before the first anchor.
  - Root also holds the host pairing key today, so root can rewrite R-MEM too. The anchor is
    only as strong as 4.1/4.2 and a key without R-MEM rights for everything else (4.3). Build cra-04 first.
  - TROPIC01 allows one SPI session at a time, so signing has to coexist with OpenVPN and the
    AWS client (batch the anchoring, retry on busy).
  - Anchoring frequency trades the tamper window against R-MEM write wear; check a slot's
    endurance in the datasheet first. *M.*
- **6.4 Trustworthy time.** Syslog dates are wrong until NTP succeeds (the image build date
  was seen), and audit lines carry `uptime=` for that reason. The image includes the
  `pcf8563` RTC driver: check whether the board has a battery-backed clock and use it. *S.*
- **6.5 More event sources** if wanted: firewall drops, DHCP, per-command audit, alerting,
  optional remote forwarding. Each adds personal data and flash writes. *M.*

## 7. Test infrastructure

- **7.1 The board hangs.** It stalled several times (after a `reboot`, after flashes) and
  needed both power and the USB debug cable unplugged. Cause unknown; a different USB-C supply
  and cable are the first thing to try. Several "hangs" turned out to be a second program
  reading the serial port or a boot that had not finished, so rule that out first. *S-M.*
- **7.2 Switchable power** (a smart plug or relay) so the harness can recover a hung board and
  run flash-and-test cycles unattended. *S.*
- **7.3 Flashing from the harness.** Failsafe entry and `sysupgrade` still need a person
  (press `f` during boot). With 7.2 this can be scripted. *M.*
- **7.4 Wired bench link** is in place (192.168.9.0/24 on a spare NIC, no default route).
  Make the setup a script, and add the LAN-side tests (SSH password login, LuCI blank login
  and the first-login gate) to the harness. *S.*
- **7.5 CI for what can run without a board.** *Done:* lint, the sandbox and tropicctl tests, doc links, a secret scan
  and a clean build.

## 8. Annex I points no feature covers

- **8.1 Part II, vulnerability handling.** An SBOM is generated and disclosure is in
  [SECURITY.md](../../SECURITY.md). A published support period is missing: a Tropic Square
  decision, not a code change.
- **8.2 2(c) update delivery and notification** is not addressed (sysupgrade is manual).
- **8.3 Walk the full Annex I list once** against [annex-i-mapping.md](annex-i-mapping.md) and
  add a line (done, partial, not applicable, or out of scope with a reason) for every point
  that has none. *S.*

## 9. Web UI for the CRA features (nothing built)

- **9.1 A LuCI page to manage the CRA features.** For an owner without a serial console or
  SSH (the first password, 6.1, is the entry point). One place in the web UI to:
  - see the state of each feature at a glance (password set and console login on, security
    log on and its size, vault present, unlocked and which slot, VPN and AWS up);
  - enable and disable features that have an opt-out (the security log's `enable` / `disable` /
    `clear`, the vault's open and close, VPN and AWS services), with plain wording about what
    each switch does and does not protect;
  - **upload the VPN and AWS configuration** (OpenVPN CA, `ta.key` and client config; the AWS
    endpoint, client ID and topic) so a unit can be set up without shell access, with validation
    before anything is written;
  - read the security log;
  - run the destructive actions (vault `init` and `destroy`, factory-reset-style erase) only
    behind an explicit confirmation that names what is lost.
  Design questions to settle first: it ships as a LuCI app (JS views plus an rpcd ucode or shell
  backend with its own ACL, so privileges are per action, not "root can do anything"); secrets
  must never be displayed or logged (only presence and fingerprint); uploads are size-limited and
  validated and written `0600`; actions that need TROPIC01 share its single SPI session, so they
  retry or stop OpenVPN and AWS first; destructive actions need CSRF protection and a
  re-authentication; and everything depends on the web UI being reachable only on the LAN over
  HTTPS. Needs 6.1 (a password must exist before the page does anything). It also widens the
  attack surface of an image that already ships LuCI, which the cra-01 README should say.
  *L.*

## 10. cra-07 secure update (in progress)

Annex I 2(c), 2(f) for the update path. Design: [cra-07 design](../../features/cra-07-secure-update/docs/design.md).

- **10.1 Router side.** *Done in the sandbox:* `tropic01-update` and the sysupgrade hook (signature chain to a root key in
  R-MEM, security version against the counter floor, refusal not forceable, commit at boot); 66 checks with 10.2. *S.*
- **10.2 Build side.** *Done:* OpenWrt patch 0003 (`tropic01_sv` in the signed metadata), the harness step
  (`TROPIC01_SIGNING_DIR`, `TROPIC01_IMAGE_SV`; nothing left behind when unset), the running image's security version in the
  root filesystem, and the host tool `tropic01-update-keys.sh`. Docker builds do not pass the signing directory in yet. *M.*
- **10.3 Spare chip.** *Done 2026-10-11 (hardware report):* root key in R-MEM, counter initialised, signed images accepted,
  unsigned, tampered, foreign-root and rolled-back images refused and not forceable, floor raised by exactly the right steps
  after boot. Open: the OpenVPN stop and restart around the check on a unit that runs OpenVPN; the commit message is logged
  twice. *S.*
- **10.4 Certificate validity and the clock.** `ucert` certificates expire (one year by default); the router's clock is wrong
  until NTP. Decide the validity policy. *S.*
- **10.5 Revocation.** How `ucert -R` records a revoked release key, and keeping that state in the chip. *S-M.*
- **10.6 Narrow who may write the root key slots and initialise the counter** (cra-04 key split, then optionally I-config).
  *Designed (cra-07 design, section 7); `tropicctl config deny` builds the restricted configuration.* Open: the decisions in
  section 7 (R-MEM write, owner key slot and custody, reversible or permanent, which chip) and a chip run. *M.*
- **10.7 Release key inside a TROPIC01 on the build side.** *M.*
- **10.8 CI.** Build OpenWrt's `usign`, `ucert` and `fwtool` in CI so `secure-update.sh` runs there instead of skipping. *S.*

## Decisions already taken

- Slot 0 (the shared key) stays valid (restricted on provisioned chips, never invalidated); the bench unit and a spare
  chip are provisioned (2026-10-11). The vault and the other features are described honestly against that.
- LuCI stays in the default image (decided 2026-10-05); the first-login gate is 6.1.
- The branch goes to `main` as **one pull request** for all features (decided 2026-10-05).
- The vault protects one folder, not the whole overlay; whole-overlay encryption is 5.4.
- The serial console is the only place a first password can be set on a fresh unit; SSH
  refuses blank passwords by design.
