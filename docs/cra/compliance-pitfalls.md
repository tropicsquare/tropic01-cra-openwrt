# CRA compliance vs. development velocity: known pitfalls

Several CRA Annex I requirements this repo implements are individually
correct and each make a real, defensible improvement — and each one also
makes fast, iterative hardware development slower or more annoying. That
tension is real, not a sign either side is wrong. The danger isn't the
tension itself; it's *quietly resolving it* by disabling the security
property and forgetting to say so. This file is where that trade-off gets
written down instead.

If you hit a new one, add it here rather than solving it silently in a
commit that doesn't mention the trade-off.

## Unique per-unit root credential (cra-01) vs. one shared dev image

**The requirement.** Annex I 2(b) credential quality: no formula-derivable
default password. The real fix — a random password generated per unit,
baked into that unit's own image before it ships — is implemented in
[`cra-01-secure-defaults/host-tools/tropic01-gen-credential.sh`](../../features/cra-01-secure-defaults/host-tools/tropic01-gen-credential.sh).

**The friction.** A unique credential per unit means a unique *image* per
unit (`build/openwrt/files/etc/shadow` is baked in at build time, before
squashfs packaging). That's fine for a production batch. It's actively
hostile to development: you can no longer build one image, flash it to
every bench unit, and SSH into whichever one is plugged in — each unit
now needs its own remembered password, and reflashing a unit for an
unrelated reason regenerates a *different* one unless you re-run the
exact same generator invocation and keep its manifest around.

**What this repo does about it.** A plain `make build` still ships a blank
root password (the generator is a host-tool you opt into), but the blank
password can no longer be used quietly: `profile.d/95-tropic01-force-password.sh`
forces `passwd` before any interactive shell is usable, then sets
`ttylogin=1` so the serial console asks for the password afterwards.
`95-tropic01-secure-defaults` also points dropbear's pre-auth banner and the
console MOTD at `banner.tropic01-insecure-default`. This is **not** a new
hole — dropbear already refuses SSH login with a blank password regardless
of `RootPasswordAuth` (confirmed on real hardware: the stock serial console
runs `login -f root` and skips auth entirely, network SSH does not), so the
hook targets exactly the console path where blank was silently usable.

The cost for development is that every fresh flash needs someone at the
console. The escape hatch is explicit and opt-in:
`host-tools/tropic01-gen-test-key.sh` makes a fresh per-run SSH key, bakes
the public half in, and drops `/etc/tropic01-dev-build`, which makes the
hook skip itself. Such an image is deliberately less strict and is a
process rule, not a technical gate — nothing stops it shipping.

**Still open.** No forced first-login web page exists (LuCI is in the image but only
nags, and the repo has no patch point in it); that design is recorded in the cra-01 README. There's
also no enforcement that a *production* batch actually ran the generator or
skipped the dev key before flashing — that's a process discipline, not a
technical gate, and nothing here checks for it. See the "per-unit images
vs. one signed fleet image" tension already called out in
[`cra-01-secure-defaults/README.md`](../../features/cra-01-secure-defaults/README.md#per-unit-images-vs-one-signed-fleet-image)
for the harder version of this problem once cra-02's fleet-wide signed
image lands.

## Shared TROPIC01 pairing key (cra-04) vs. any dev unit "just working"

**The requirement.** Annex I 2(d): every unit should authenticate to its
own chip with its own secret, not a key anyone can look up
(`003-use-engineering-sample-keys.patch` currently uses the published
`ENGINEERING_SAMPLES_01` key for every unit built from this feed — see
[`cra-04-access-control/README.md`](../../features/cra-04-access-control/README.md)).

**The friction.** This is the *sharpest* version of the same tension.
Per-unit pairing keys mean a brand-new dev unit's chip is *unusable* —
not just "has a guessable password" but literally cannot open a secure
session — until someone provisions it individually. A shared engineering
key is exactly what makes "flash any unit, SSH in, `tropicctl info chip` just
works" possible today. That convenience and Annex I 2(d) are in direct
tension; there is no config flag that gets both.

**What this repo does about it.** Nothing yet — cra-04 is explicitly
"partial," and the README already refuses to claim otherwise. Flagging it
here because it's the same shape of problem as the credential one above,
just with a harder consequence (a bricked-feeling chip, not just a
password prompt) and a genuinely harder fix (per-unit pairing keys need
their own provisioning step, and slot-0 invalidation is irreversible —
see that feature's `CLAUDE.md`).

## `sysupgrade` config preservation doesn't cover everything you'd assume

Not a security-vs-velocity trade-off exactly, but a real pitfall
discovered the same way: assuming a "keep settings" reflash preserves
*more* state than it actually does costs you working access, not just
convenience.

- **`/etc/shadow` is *always* preserved** (`lib/upgrade/keep.d/base-files-essential`
  lists it unconditionally). A newly baked-in credential from
  `tropic01-gen-credential.sh` will **not** take effect on a unit that's
  already been provisioned once — the old hash wins, silently, on every
  future reflash. If you need to actually change a unit's password, set
  it directly (`passwd`), don't rely on reflashing with a new baked-in
  shadow file.
- **Two different "authorized_keys" files exist** and only one is what
  dropbear actually reads (`/etc/dropbear/authorized_keys`, not
  `root/.ssh/authorized_keys`), and only the former is in dropbear's
  default `keep.d` list. A key added only to the latter doesn't survive
  a flash that wipes and restores from the keep-list.

## Debug-build conveniences that must not reach anything resembling a release

Not CRA-specific, but the same "quiet erosion" shape: `tropicctl` is built
with `LT_PRINT_SPI_DATA=1` (raw SPI bus traffic to stdout) and
`libtropic-pkcs11` carries syslog tracing
(`006-add-syslog-tracing.patch`) — both genuinely useful while bringing
up new hardware, both things you do not want logging chip traffic on a
unit anyone else can read `logread` on. Tracked in the [CRA backlog](backlog.md), item 3.10; flagged here too since
it's the same category of problem — a dev-velocity aid with a real cost
if it silently ships.
