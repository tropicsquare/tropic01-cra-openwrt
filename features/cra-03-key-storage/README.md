# cra-03-key-storage — confidentiality of stored and transmitted data

**CRA Annex I, Part I, 2(e). Status: done for OpenVPN. AWS IoT telemetry is not part of this
feature's claim: it is in the backlog (3.1) until it works.**

**Verified 2026-10-05 on an OpenWrt One, final image:** OpenVPN with the client key in
TROPIC01 (PKCS#11) and the certificate in R-MEM slot 1 (`CN=tropic01-client`, which verifies
against the CA of the VPN it connects to). `tun0` came up in about 5 s with the server-pushed
route, 4 of 4 pings through the tunnel to the VPN gateway succeeded (about 8 ms), the OpenVPN
process held the TROPIC01 SPI device, and after a reboot with the instance enabled the tunnel
came up by itself at 48 s of uptime (3 of 3 pings). The VPN configuration was supplied by the
owner (a private, git-ignored `tropic01-client.ovpn`), a PKCS#11 client config was derived from it
with the chip's object id, and the CA, TLS key and config were removed from the unit afterwards.
**Verified again 2026-10-11** on a clean rebuild flashed on the router: the tunnel came up, 4 of 4
pings, and the log shows the module signing in TROPIC01. The module picks the slot-0 key from
`/etc/tropic01/slot0-key`.
**Out of scope until fixed (backlog 3.1):** AWS IoT. It was already failing before this branch (the OpenVPN/AWS SPI
hand-off uses a fixed `sleep 2`, and AWS rejected the MQTT connect, likely a certificate or
IoT policy problem on the AWS side).

**To set up a VPN identity** (generate the key in TROPIC01, get a certificate, install a `.ovpn`), follow the
[OpenVPN user guide](docs/openvpn-user-guide.md); `host-tools/tropic01-install-ovpn.sh` does the install step.

## At a glance

| | |
|---|---|
| Requirement | CRA Annex I, Part I, 2(e): confidentiality of stored and transmitted data |
| Status | done for OpenVPN; AWS IoT is in the backlog and not claimed |
| What it does | OpenVPN client whose private key lives in TROPIC01 and signs through PKCS#11 |
| Start here | [docs/openvpn-user-guide.md](docs/openvpn-user-guide.md) |

## Role of TROPIC01

**Central.** The VPN client's private key is generated inside the chip (ECC slot N) and can
never be read out; every TLS handshake asks the chip to sign. The matching X.509 certificate
(public) is stored in R-MEM slot N+1 so the router can present it. Someone who copies the
router's flash gets no VPN key. The chip is reachable by whoever holds the board and the
pairing key. That is the shared engineering-sample key, unless the unit was provisioned with
`tropic01-pairing` (cra-04); even then the per-unit key is on the router's flash. So this
protects against flash extraction, not against physical possession of the whole unit.

## Scope

Annex I 2(e) asks that products "protect the confidentiality of stored,
transmitted or otherwise processed data ... by encrypting relevant data at
rest or in transit ... by using state of the art mechanisms". This feature
does that for OpenVPN, authenticated with a private key that is generated
inside TROPIC01 and never leaves it (see [core](../core/README.md)):

- **OpenVPN** client authentication (mutual TLS to a VPN concentrator)
- **AWS IoT Core** device identity (mutual TLS, MQTT over TLS on port 8883)
  uses the same mechanism, but it does not work yet and is not claimed
  (backlog 3.1)

Because the private key never leaves the chip, a flash dump of the router
does not yield the VPN credential — the strongest version of "confidential
by design" this project can currently offer.

## Packages

| Package | Purpose |
|---|---|
| `openvpn` | OpenVPN 2.6.12 built with PKCS#11 (pkcs11-helper), with an init script tuned for TROPIC01 start-up |
| `tropic01-aws` | AWS IoT Core MQTT client — publishes telemetry via TROPIC01 mTLS |
| `coreMQTT` | FreeRTOS coreMQTT client library, statically linked into `tropic01-aws` |

## Docs

| Doc | Topic |
|---|---|
| [`docs/solution-description.md`](docs/solution-description.md) | Architecture overview (VPN + AWS IoT) |
| [`docs/vpn-router-setup.md`](docs/vpn-router-setup.md) | Full VPN provisioning walkthrough |
| [`docs/openvpn-pkcs11.md`](docs/openvpn-pkcs11.md) | OpenVPN + PKCS#11 config details |
| [`aws/README.md`](aws/README.md) | AWS IoT trust-anchor certificates |
| [`examples/`](examples/) | OpenVPN client config templates (`.example`, safe to commit) |

## Building

```sh
make package PKG=openvpn
make package PKG=tropic01-aws
```

Both are selected by default in `openwrt.config` (`openvpn_openssl`,
`tropic01-aws`); the full image build includes them.

## Router-side config files

| File | Purpose |
|---|---|
| `/etc/openvpn/tropic01-pkcs11-client.conf` | OpenVPN client config (PKCS#11 auth) |
| `/etc/config/openvpn` | UCI config — enables procd autostart |
| `/etc/ssl/tropic01-aws/openssl-aws.cnf` | OpenSSL PKCS#11 provider config |
| `/etc/ssl/tropic01-aws/AmazonRootCA1.pem` | Amazon Root CA for AWS IoT TLS; not installed by the package, copy it from [`aws/`](aws/) |
| `/etc/config/tropic01-aws` | UCI config — endpoint, topic, telemetry interval |

## What this does NOT fix

- **Data at rest.** The overlay filesystem itself is not encrypted — only
  the two channels above use TROPIC01-backed keys. Encrypting the overlay
  is [cra-05-secure-erase](../cra-05-secure-erase/)'s prerequisite, not
  done here.
- **The host↔chip channel uses the shared pairing key** on units not
  provisioned with `tropic01-pairing`. See
  [core](../core/README.md)'s "What this does NOT fix" —
  [cra-04-access-control](../cra-04-access-control/) is where that gets
  fixed, not here.
- **OpenVPN is pinned at 2.6.12**, not the latest 2.6.x security-fix
  release. Bumping it needs a hardware test (PKCS#11 + procd respawn
  timing were tuned against this exact version) before it can move.
