# cra-01-secure-defaults — secure-by-default configuration

**CRA Annex I, Part I, 2(b). Status: partial.**

## At a glance

| | |
|---|---|
| Requirement | CRA Annex I, Part I, 2(b): secure by default configuration, reset to original state |
| Status | partial |
| What it does | forces a real root password before a shell is usable, tightens SSH and the firewall defaults, refuses empty passwords |
| Hardware needed | none |

## Role of TROPIC01

**None today.** Secure defaults are configuration and policy; a secure element cannot make a
password stronger or close a port, and this feature does not pretend otherwise. TROPIC01 becomes
relevant to this requirement only for the "reset to original state" part, where a verified,
signed factory image (cra-02) and the chip's monotonic counters could prevent a rollback to an
insecure image. That is future work, not claimed here.

## Scope (read this before extending this feature)

Annex I 2(b) asks for products "with a secure by default configuration,
including the possibility to reset the product to its original state."
That single sentence actually asks for three different things, and only
two of them are things a secure element (or any hardware) can help with:

| Sub-requirement | Hardware-fixable? | Where it lives in this feature |
|---|---|---|
| Attack surface (closed ports, no default-open services) | **No** — firewall and image configuration, full stop | `tropic01-secure-defaults/files/95-tropic01-secure-defaults` (policy assertion + drift warnings only) |
| Credential quality (no formula-derivable default password) | Yes, if a real entropy source produces it | `host-tools/tropic01-gen-credential.sh` (build-host script, run once per unit before flashing); default builds instead force the owner to set a password at first interactive login (`profile.d` hook plus a boot-time guard that also rejects an empty-password hash) — console and key-based SSH only; LuCI is present but does not force one |
| Reset integrity (factory reset restores a *verified* baseline, not just whatever is in flash) | Yes, if reset can check a signature against an immutable key | **Not implemented yet** — depends on [cra-02-verified-boot](../cra-02-verified-boot/); see below |

Don't let this feature's existence be read as "secure-by-default: done."
Two of the three rows above needed no chip at all, and the third genuinely
isn't finished.

## What's implemented today

**`tropic01-secure-defaults`** — a standard `/etc/uci-defaults` package
(runs once at first boot, then OpenWrt removes it automatically). It:

- re-asserts, explicitly and auditably in one file, that the WAN firewall
  zone's `input`/`forward` policy is `REJECT` (defense-in-depth — stock
  OpenWrt already ships this; the point is that it is now asserted by a
  script under version control instead of "whatever the last person to
  touch `/etc/config/firewall` left behind");
- pins `dropbear` (SSH) to the `lan` interface only;
- disables SSH password login **once a key has actually been
  provisioned** (`/etc/dropbear/authorized_keys` exists and is non-empty)
  — until then password login stays on, so an installer with nothing else
  to log in with is never locked out;
- warns (via `logger`, does not change anything) about any WAN-facing
  firewall `rule` or `redirect` outside the stock allowlist, so drift from
  the baseline is visible instead of silent;
- if root still has no password set — the shipped default for any build
  that skipped the credential generator below, which is every `make
  build` unless you deliberately ran it first — points dropbear's
  pre-auth banner and the console MOTD at
  `/etc/banner.tropic01-insecure-default`, so the gap is loud on every
  connection attempt instead of invisible until someone goes looking.
  (Blank password over SSH is refused by dropbear regardless — confirmed
  on hardware — so a blank password is not an open network door.) See
  [docs/cra/compliance-pitfalls.md](../../docs/cra/compliance-pitfalls.md)
  for why a unique per-unit credential isn't the default for every build.

**`/etc/profile.d/95-tropic01-force-password.sh`** — sourced by `/etc/profile`
for every interactive login shell. While root has no password it loops
`passwd` (Ctrl-C ignored; five failed attempts close the session) until a
real password exists, then sets `system.@system[0].ttylogin=1` so the serial
console asks for that password from then on. This is the enforcement point
that works on stock OpenWrt: the console normally runs `login -f root`
(pre-authenticated, no prompt), and dropbear never offers a shell to a
blank-password login, so the hook covers exactly the path where a blank
password is otherwise silently usable. It is skipped for non-tty sessions
(so `ssh host cmd` and scripts never hang) and for dev builds carrying
`/etc/tropic01-dev-build`. Recovery from a lost password is OpenWrt failsafe
mode.

**`/usr/libexec/tropic01-console-guard`** (+ `/etc/init.d/tropic01-console-guard`,
S99) — the shared check, run by the hook and at every boot. "No password"
is not just an empty hash field: an empty password stored as a real hash
looks set to any test of that field, yet dropbear, rpcd and login accept
`""`. This happened on hardware when LuCI's first-login screen accepted an
empty password (see "First-login web page"). The guard therefore also asks
rpcd (`ubus call session login`, the verifier LuCI uses) whether `""`
unlocks root; if it does, the hash is cleared with `passwd -d` so the
forced-password flow applies again. Once a real password exists it sets
`ttylogin=1` (idempotent, so a write lost to the first-boot race, or to a
session that ended early, is repaired at the next boot) and removes the
first-boot blank-password warnings. Limits: it needs rpcd to test (`unknown`
if rpcd is not up yet; the boot-time run covers that), and a doubtful reading
is treated as "password set" so an account is never blanked on a guess.

**`openwrt-patches/0002-busybox-refuse-empty-password-in-passwd.patch`** —
busybox `passwd` applied its weak-password check to non-root only, so root
could set `""` from the console or piped on stdin (LuCI's `setPassword`
does exactly that). The patch refuses an empty new password for every
caller; `passwd -d` still works.

**`host-tools/tropic01-gen-test-key.sh`** — dev/bench only, never a
production step. Each run makes a fresh ed25519 keypair, bakes the public
half into `files/etc/dropbear/authorized_keys` (so SSH password login is
also turned off by the script above) and writes `files/etc/tropic01-dev-build`,
which makes the forced-password hook skip itself. The private half goes to
a directory outside any git work tree (the script refuses otherwise). It
is not run by `make build`: a plain build trusts no key and keeps the
forced-password hook. An image made this way is deliberately less strict
and must never ship.

**`host-tools/tropic01-gen-credential.sh`** — run by whoever
builds/provisions a unit, *before* flashing, never on the router and never
at first boot (a first-boot script has no better entropy than a build-host
script, and regenerating the password every boot would overwrite a
password the owner already changed). It writes the full `etc/shadow` file
(root's hash plus the standard non-root accounts) into that unit's
`build/openwrt/files` overlay; the plaintext is appended once to an
operator-only manifest CSV for the printed label. **That manifest must
never be committed** — the script itself refuses to write one inside a git
work tree, and `.gitignore` also excludes `*manifest*.csv` as a second
layer.

Today the script's only entropy source is `/dev/urandom` (the kernel
CSPRNG, 128 bits — already a real improvement over any serial-derived
formula). TROPIC01-sourced entropy is not implemented; the script logs
that plainly on every run rather than implying it. Wiring an actual raw
random pull from TROPIC01 needs a host-native `tropicctl`/`libtropic` build
talking to the chip over USB or a host SPI adapter — see
[libtropic](https://github.com/tropicsquare/libtropic) — which is future
work, not a prerequisite for this credential to already be a real
improvement over a derivable default.

## Building

```sh
make package PKG=tropic01-secure-defaults
```

Selected by default via `openwrt.config`
(`CONFIG_PACKAGE_tropic01-secure-defaults=y`).

`host-tools/tropic01-gen-credential.sh` is not an OpenWrt package — it is
a build-host script, invoked per unit as part of your provisioning
pipeline, before the image build that will bake in its output:

```sh
./features/cra-01-secure-defaults/host-tools/tropic01-gen-credential.sh \
    SN00123 build/openwrt/files /secure/manifests/batch-07.csv
make build   # build/openwrt/files/etc/shadow now bakes into this unit's image
```

Run `make prepare` at least once first, so the script can seed the shadow
file's non-root accounts from the actual pinned `base-files` package
instead of falling back to an embedded copy.

## What's explicitly not done

### First-login web page

The default image ships LuCI (`luci` + `luci-ssl`, over uhttpd, LAN only),
and it does **not** enforce a password. Observed on an OpenWrt One with a
blank root: LuCI let the first visitor in with an empty password, showed
a set-password screen with no warning, and accepted an empty new password.
That stored a hash of `""`, which defeated the console hook (it only looked
at the hash field) and left root accepting an empty password over the web
and, per dropbear's source, SSH. LuCI also cannot be gated from this repo:
it only nags from its theme header and has no pre-dispatch hook.

Mitigated, not solved: the busybox patch stops an empty password being
stored, and the guard clears one that already exists. But on a unit with no
password set, LuCI still lets the first person on the LAN log in blank and
choose the root password, i.e. whoever gets there first wins. So the web UI
remains extra LAN-side attack surface for any unit without a serial
console, and the first-login gap is wider with LuCI present, not closed.

The planned fix is a small LAN-only setup page served while root is blank,
with real LuCI held back until a password exists; it needs a decision on
HTTPS for that page (`luci-ssl` provides a self-signed certificate) and a
prototype.

### Backlog

Open work for this feature (the LuCI first-login gate, hardware re-checks, the `firstboot -y`
question, the per-unit versus fleet image tension) is tracked in the central
[CRA backlog](../../docs/cra/backlog.md), sections 1, 4.5 and 6.

### Gaps in the forced-password hook

- It forces a password only where a shell is reachable: the serial console
  and SSH sessions that got in by key. Nothing forces it on the network,
  by design (dropbear's refusal of blank passwords stays untouched).
- Until the hook has run once, the serial console is passwordless
  (`login -f root`); anyone with physical access to an unset unit can set
  the password first. That is the same physical-access trust the rest of
  the platform assumes, but it is not a remote-attack defence.
- A dev build (`/etc/tropic01-dev-build`) skips the hook on purpose.
  Nothing technical stops such an image from shipping; it is a process
  rule, not a gate.

### Reset integrity

A factory reset that just re-runs `firstboot` restores *whatever the
squashfs base image contains*, unverified. Real reset-integrity means
checking a signature over that baseline against the same immutable key
used for secure boot ([cra-02-verified-boot](../cra-02-verified-boot/))
before applying it. That feature doesn't exist in this repo yet — no
U-Boot verified-boot chain, no monotonic counter check — so this row stays
an open TODO until it lands. Do not build a "verified reset" on top of an
unsigned boot chain; it would verify against nothing.

### Per-unit images vs. one signed fleet image

Baking `/etc/shadow` into `build/openwrt/files` means **one image build
per unit** — every unit needs its own signed image. That is in tension
with [cra-02-verified-boot](../cra-02-verified-boot/)'s goal of one signed
image for the whole fleet, and it has a sharp edge: a fleet-wide
sysupgrade followed by a factory reset restores the stock, *empty* root
password from the shared base image, not the unit's original per-unit
password.

The planned fix is to store the per-unit password hash (or, better, an SSH
public key) in TROPIC01 R-MEM at provisioning time, and have a
uci-defaults script copy it into `/etc/shadow` on first boot from a
single, fleet-wide image. That would also make factory reset restore the
*correct* per-unit credential, closing part of the reset-integrity gap
above without waiting on cra-02. **Not implemented.**

## Testing

The credential script is host-side and can be tested without hardware —
run it twice with different serials into a scratch directory and confirm
each run produces a different plaintext, a valid `sha512-crypt` hash, and
that all five stock accounts (`root`, `daemon`, `ftp`, `network`,
`nobody`) survive.

The test-key script is also host-side: run it twice into a scratch
directory, confirm two different keypairs, one line in `authorized_keys`,
modes 700/600, and that a key directory inside a git work tree is refused.

On an OpenWrt One the following was run (console, plus
`tests/hardware/bench.py` for the unattended cycle): the forced password at a
true first boot; an empty password refused on every attempt and the session
closing after five; a real password accepted; `ttylogin=1`; the stale
first-boot MOTD and dropbear banners removed; an empty-string hash planted,
detected by the guard and cleared both by hand and by the S99 boot-time run
(before any login); the `ttylogin=0` repair; the dev-marker skip; LuCI
answering over HTTP and HTTPS and rpcd rejecting an empty password; and
`tropicctl` reading the chip ID, the slot-1 public key and the slot-1
certificate with the current image. Also in a sandbox (fake `passwd`/`uci`/
`ubus`, pseudo-tty): every hook and guard branch. The first clean boot of
this image once left a root hash set with `ttylogin` at 0; the cause is not
proven (a console session ending between `passwd` and the `uci` write, or the
first-boot race with `config_generate`), and the guard now repairs that at the
next boot whatever the cause.

The uci-defaults script cannot be tested without a router: it depends on
`/lib/functions.sh` and UCI, neither of which exist on a build host. It
was reviewed for logic and passes `make lint`; it has not been run on
real hardware. Say so in any PR that touches it, and get an on-hardware
test before merging into `main` (see `CONTRIBUTING.md`).
