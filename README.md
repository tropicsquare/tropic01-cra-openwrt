# TROPIC01 CRA reference build for OpenWrt One

A worked example of building a networked product that takes the EU **Cyber Resilience Act (CRA)**
seriously: an [OpenWrt One](https://openwrt.org/toh/openwrt/one) router, a
[TROPIC01](https://tropicsquare.com) secure element, and the CRA's Annex I security requirements
worked through one at a time. Every requirement becomes a *feature*: a package, documentation, tests, and
an honest statement of **what it fixes and what it does not**.

> **This is a reference build, not a product, and not legal advice.** It shows how requirements map to
> code and hardware, and where a secure element does and does not help. Nothing here, on its own, makes a
> product CRA-compliant.

## Why this project exists

The CRA asks for things like "secure by default", "protect confidentiality of data", "limit attack
surfaces", "allow secure erase of data". It is easy to answer with a checklist. It is harder, and more
useful, to ask for each point: **what can hardware actually fix, and what is plain engineering policy that
no chip can do for you?**

A secure element is genuinely strong at some of it (a private key that is generated inside the chip and
can never be copied out). It contributes nothing to other parts (which network ports are open). This
repository keeps both answers visible, per requirement, so that "security by design" does not quietly turn
into "security by checklist". If you want to see a CRA clause implemented end to end, with the TROPIC01
role stated plainly and the limits written down, you are in the right place.

**Who it is for:** engineers and product owners preparing for the CRA; people evaluating TROPIC01;
OpenWrt developers who want a PKCS#11 hardware-key setup; anyone who wants a tested, honest example.

## What is in the image

Each row is a folder under [`features/`](features/) with its own README.

| Feature | CRA Annex I | What it does | Role of TROPIC01 | Status |
|---|---|---|---|---|
| [core](features/core/) | n/a | the chip's software layer: `libtropic`, a PKCS#11 module, the `tropicctl` tool | the interface to the chip | working; every `tropicctl` command except `pairing invalidate` run on a chip |
| [cra-01-secure-defaults](features/cra-01-secure-defaults/) | 2(b) secure by default | closed WAN, SSH on the LAN only, a root password is forced before any shell, empty passwords are refused | **none today** (policy and configuration) | partial |
| [cra-02-verified-boot](features/cra-02-verified-boot/) | 2(f), 2(c) integrity, rollback | signed boot and anti-rollback | planned: a monotonic counter for anti-rollback. It is **not** the boot root of trust | not started; signed updates with rollback protection are in progress on a branch |
| [cra-03-key-storage](features/cra-03-key-storage/) | 2(e) confidentiality | OpenVPN client whose private key lives in the chip | **central:** generates the key, signs inside the chip, stores the certificate | done for OpenVPN |
| [cra-04-access-control](features/cra-04-access-control/) | 2(d) authentication | per-unit keys for talking to the chip | **central:** pairing keys and access privileges | partial: per-unit key on the bench unit and a spare chip; slot 0 restricted, not invalidated |
| [cra-05-secure-erase](features/cra-05-secure-erase/) | 2(m) secure data removal | an encrypted vault that can be destroyed cryptographically | **central:** random key and key storage, erased on destroy | partial: one folder, not the whole disk |
| [cra-06-security-logging](features/cra-06-security-logging/) | 2(l) logging with opt-out | a security event log and a change audit | **none today** (a signed log is on the backlog) | partial |

What is still open, with dependencies and order, is in the [CRA backlog](docs/cra/backlog.md). The
requirement-by-requirement analysis is in [docs/cra/annex-i-mapping.md](docs/cra/annex-i-mapping.md).

### Status at a glance

**Done and tested on a real OpenWrt One** (details and dates in the
[hardware test report](docs/cra/hardware-test-report.md)), with a TROPIC01 engineering sample and a second chip with
production provisioning:

- the forced first password and the boot-time guard (cra-01);
- the OpenVPN tunnel with its key in the chip, including key generation, a chip-signed certificate request and storing
  the certificate (cra-03), re-verified on a clean rebuild in October 2026;
- the encrypted vault and its crypto-erase (cra-05), re-verified on the same rebuild;
- the security log and change audit (cra-06);
- a per-unit pairing key written to the chip and the shared key's rights taken away (`tropic01-pairing`, cra-04);
- every `tropicctl` command that writes the chip except `pairing invalidate`: ECC keys, R-MEM, pairing write,
  configuration rewrite, MAC-and-Destroy PIN, counters, an I-config bit, and a chip firmware update.

**Not done, and said plainly:**

- **Updates are not authenticated.** In this build `sysupgrade` accepts any structurally valid image: the OpenWrt One
  check verifies the image's hashes, not who made it, and the signature check passes because `ucert` is not in the image.
  Signed updates with a rollback floor in TROPIC01 are in progress on a branch, not merged.
- **No secure boot** (cra-02). Whether this board's boot ROM can verify a signature at all is still to be found out.
- **Per-unit chip pairing keys** are provisioned only on one bench unit and a spare chip; every other unit still talks
  to its chip with the published shared key. Slot 0 is restricted on provisioned units, never invalidated.
- **Only the vault folder is encrypted**, not the whole device.
- **AWS IoT telemetry does not work.**
- **On a unit with no password set,** the first person on the LAN can choose it through the web UI.
- The PKCS#11 module does not take `tropicctl`'s chip lock, so the two must not use the chip at the same time (the
  scripts stop OpenVPN around their chip access).

## What you need

- **Board:** an OpenWrt One (MediaTek MT7981B, Cortex-A53).
- **Secure element:** a TROPIC01 on a MikroE Click board in the board's mikroBUS socket (SPI
  `/dev/spidev2.0` at 1 MHz, chip select on GPIO 25). Only this SPI connection is supported. An engineering sample
  works as it is; a chip with production provisioning needs `echo prod0 > /etc/tropic01/slot0-key` on the router.
- **Cables:** USB-C for power, a second USB-C to the board's **debug port** (the serial console), and an
  Ethernet cable.
- **A build host:** Linux with the [OpenWrt build dependencies](https://openwrt.org/docs/guide-developer/toolchain/install-buildsystem),
  about 40 GB free disk and 8 GB RAM, and internet access for sources. Or just Docker.

## 1. Build

```sh
git clone https://github.com/tropicsquare/tropic01-cra-openwrt.git
cd tropic01-cra-openwrt
make                     # clone the pinned OpenWrt, apply patches, build the image
# nothing installed but Docker?
make docker-build
```

The first build takes one to several hours; later builds are much faster. Output is in
`build/openwrt/bin/targets/mediatek/filogic/`:

| File | Use |
|---|---|
| `…openwrt_one-squashfs-sysupgrade.itb` | **upgrade a unit that already runs OpenWrt** (what you want in most cases) |
| `…openwrt_one-snand-factory.bin`, `…-nor-factory.bin` | first-time install from U-Boot; see the [OpenWrt One page](https://openwrt.org/toh/openwrt/one) |
| `…openwrt_one.bom.cdx.json` | the software bill of materials (CycloneDX) |
| `sha256sums` | checksums |

Useful targets (`make help` lists them all): `make package PKG=<name>` rebuilds one package, `make menuconfig`
changes the configuration, `make DL_DIR=~/openwrt-dl` shares a download cache, `make lint` and `make test`
run the checks that need no board. Never edit `build/openwrt` by hand: it is regenerated from this repository
([how it is organised](#how-the-repository-is-organised)).

## 2. Flash

From a unit that already runs OpenWrt (SSH or the serial console):

```sh
scp -O openwrt-mediatek-filogic-openwrt_one-squashfs-sysupgrade.itb root@192.168.1.1:/tmp/fw.itb
ssh root@192.168.1.1 sha256sum /tmp/fw.itb        # compare with sha256sums
ssh root@192.168.1.1 sysupgrade -T /tmp/fw.itb    # test the image format (it does not check who made it)
ssh root@192.168.1.1 sysupgrade -n -v /tmp/fw.itb # flash, discarding old settings
```

Use `-n` for a clean image. Kept settings are restored from a backup archive, not carried over in place,
and can clash with a newer image.

**If you cannot log in** (a lost password, say): open the serial console (below), reboot, and when
`Press the [f] key and hit [enter] to enter failsafe mode` appears, press `f` and Enter. Failsafe has no
password. The LAN port is not configured, so bring it up by hand (`ip link set eth1 up; ip addr add
192.168.1.1/24 dev eth1`), copy the image over, and run `sysupgrade` as above.

> **Known hazard: the board can stall after a flash.** After `sysupgrade` (and occasionally after `reboot`)
> the serial console may print only a few `F0:…BP:` lines and then go silent. This is a hardware-level
> stall, not your image. Unplug **both** the power and the USB debug cable for about a minute, then plug in
> the USB cable first and the power last. It was seen after most flashes during development; the cause is not
> understood ([backlog 7.1](docs/cra/backlog.md)).

## 3. First boot

Connect the debug USB-C port and open the serial console (one program at a time; two readers split the
bytes and make the board look dead):

```sh
picocom -b 115200 /dev/ttyACM0          # or minicom / screen
```

A fresh image has **no root password**, and nothing lets you log in remotely until one is set (SSH refuses
blank passwords). On the console you will see:

```
 This unit has NO ROOT PASSWORD (CRA Annex I 2(b)). You must set one
 before you can use this shell.
Changing password for root
New password:
```

Choose a password. Console login is required from the next login on, and the warnings disappear.
An empty password is refused everywhere: console, `passwd`, and the web UI.

Then, from a computer on the router's **LAN port** (the 1 Gbit port; the 2.5 Gbit port is WAN):

```sh
ssh root@192.168.1.1
https://192.168.1.1        # LuCI web UI, self-signed certificate
```

If your upstream network also uses `192.168.1.0/24`, move the router's LAN (stock OpenWrt behaviour):
`uci set network.lan.ipaddr=192.168.9.1/24 && uci commit network && /etc/init.d/network reload`.

## 4. Use the features

Check the chip first: `tropicctl info chip` prints its identification. It must not fail.

| I want to | Do this |
|---|---|
| connect the router to a VPN with a key that never leaves the chip | follow the [OpenVPN user guide](features/cra-03-key-storage/docs/openvpn-user-guide.md) (generate the key, make a certificate request, get it signed, install the profile); `host-tools/tropic01-install-ovpn.sh` does the install step |
| keep some data in an encrypted vault that can be destroyed | `tropic01-vault init` (writes R-MEM slot 100), data in `/vault`, `tropic01-vault destroy --yes-destroy-vault` to erase the key and the image. See [cra-05](features/cra-05-secure-erase/) |
| see or switch off the security log | `tropic01-security-log status \| show \| disable \| enable \| clear`. See [cra-06](features/cra-06-security-logging/) |
| use a chip with production provisioning | `echo prod0 > /etc/tropic01/slot0-key` (an engineering sample needs nothing). `tropicctl session test` tells you whether the key fits |
| see or update the chip firmware | `tropicctl fw status`; `tropicctl fw update --dir <images> --yes-update-firmware` cannot be undone (see [tropicctl](features/core/tropicctl/README.md)) |
| make a per-unit chip pairing key | `features/cra-04-access-control/host-tools/tropic01-gen-pairing-key.sh`. It only generates files; nothing is written to the chip. See [cra-04](features/cra-04-access-control/) |
| put the per-unit key on the chip and take the shared key's rights away | `tropic01-pairing status`, then `tropic01-pairing provision --yes-write-pairing-slot --yes-rewrite-r-config` (`undo` restores the old access rights). The slot write is one way. See [cra-04](features/cra-04-access-control/) |
| bake a unique root password into an image | `features/cra-01-secure-defaults/host-tools/tropic01-gen-credential.sh`. See [cra-01](features/cra-01-secure-defaults/) |
| get SSH access to a bench image without typing a password each flash | `features/cra-01-secure-defaults/host-tools/tropic01-gen-test-key.sh` (**development only**: the image it builds skips the forced-password prompt) |

## Safe and irreversible operations

Building, flashing, the vault, the security log and the OpenVPN setup can all be undone. A few chip
operations cannot, and this project never runs them automatically; the docs mark each one:

| Operation | Why it matters |
|---|---|
| writing a pairing-key slot (`tropicctl pairing write … --yes-write-pairing-slot`) | a slot is written once and cannot be rewritten |
| invalidating pairing-key slot 0 | permanent; if the replacement key is lost, **the chip can never be opened again** |
| generating a key in an ECC slot, clearing it later | the private key cannot be backed up or read; clearing destroys that identity |
| erasing an R-MEM slot | the data is gone for good (it is how the vault is destroyed) |
| initialising or decrementing a monotonic counter (`tropicctl counter init/update`) | a counter only counts down |
| clearing an I-config bit (`tropicctl config burn`) | a bit never goes back to 1 |
| updating the chip firmware (`tropicctl fw update`) | no downgrade; an interrupted update can leave the chip unusable |
| rewriting the R-config (`tropicctl config rewrite`, `tropic01-pairing provision`) | a wrong file can lock keys out; it takes effect after `tropicctl chip reboot` |
| (planned) programming SoC eFuses | one way |

If you work with an AI agent, it is instructed to stop and ask before each of these (see
[SKILLS.md](SKILLS.md)).

## Test it

```sh
make lint          # shellcheck on every shell script
make test          # sandbox checks and the tropicctl tests, against fake hardware, no board needed
python3 tests/hardware/bench.py suite   # the real-board suite: needs a board that is not provisioned, see tests/hardware/README.md
```

## How the repository is organised

This is an [OpenWrt package feed](https://openwrt.org/docs/guide-developer/feeds) plus a small patch set,
not a fork of OpenWrt:

```
openwrt.pin           the upstream OpenWrt revision this builds against
feeds.conf            upstream feeds, pinned
openwrt-patches/      the only changes made to OpenWrt itself (board device tree, busybox)
configs/              the base .config for the OpenWrt One (includes LuCI)
features/             this repo's feed: one folder per feature
  <feature>/README.md         what it does, TROPIC01's role, what it does NOT fix, how it was tested
  <feature>/openwrt.config    the config fragment it adds to the build
  <feature>/<package>/        OpenWrt package(s)
  <feature>/host-tools/       scripts that run on your computer, not on the router
docs/cra/             the CRA mapping, the backlog, the hardware test report, known pitfalls
tests/sandbox/        tests that need no board        tests/hardware/   tests that drive a real board
scripts/              prepare.sh: clone, patch, wire feeds, assemble .config; check-doc-links.py
.claude/skills/       task guides for AI agents
```

`build/` is generated.

## Documentation map

- [docs/cra/annex-i-mapping.md](docs/cra/annex-i-mapping.md): every requirement, hardware versus policy
- [docs/cra/backlog.md](docs/cra/backlog.md): everything still open, with order and dependencies
- [docs/cra/hardware-test-report.md](docs/cra/hardware-test-report.md): what was tested, on what, with what result
- [docs/cra/compliance-pitfalls.md](docs/cra/compliance-pitfalls.md): where good intentions erode
- [features/cra-03-key-storage/docs/openvpn-user-guide.md](features/cra-03-key-storage/docs/openvpn-user-guide.md): key, CSR, certificate, profile
- [features/core/tropicctl/README.md](features/core/tropicctl/README.md): the TROPIC01 command-line tool; [reference](features/core/tropicctl/docs/reference.md) and [safety classes](features/core/tropicctl/docs/safety.md)
- [features/cra-04-access-control/docs/pairing-key-design.md](features/cra-04-access-control/docs/pairing-key-design.md): per-unit pairing keys and access rights
- [tests/sandbox/README.md](tests/sandbox/README.md) and [tests/hardware/README.md](tests/hardware/README.md): the tests
- [SKILLS.md](SKILLS.md): working on this repository with an AI agent, with example prompts

## Contributing, security, license

- [CONTRIBUTING.md](CONTRIBUTING.md): branch model, conventions, what a feature needs before it merges
- [SECURITY.md](SECURITY.md): how to report a vulnerability (please not as a public issue)
- Build scripts, package Makefiles and patches are GPL-2.0-only (see [LICENSE](LICENSE)), matching OpenWrt.
  Packaged upstream software keeps its own license (`PKG_LICENSE` in each package Makefile).
