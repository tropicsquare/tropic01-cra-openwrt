# Security policy

## Reporting a vulnerability

Please **do not** report security vulnerabilities through public GitHub
issues. E-mail:

> **security@tropicsquare.com**

Include as much as you can: type of issue (e.g. key leakage, privilege
escalation, RCE), affected component and version, steps to reproduce or a
proof of concept, and the potential impact.

**Scope.** This repository's own code and configuration: the packages under
`features/`, the patches in `openwrt-patches/` and in the package `patches/`
folders, the build scripts and the image configuration. A vulnerability in
upstream OpenWrt, libtropic or another packaged project belongs to that
project; tell us too if it affects this build.

You will get an acknowledgement within **5 business days** and a
resolution timeline within **10 business days**. We follow
[coordinated disclosure](https://cheatsheetseries.owasp.org/cheatsheets/Vulnerability_Disclosure_Cheat_Sheet.html):
please do not disclose publicly until a fix is released or 90 days have
passed, whichever comes first.

## Supported versions

| Version | Supported |
|---|---|
| `main` | yes |
| feature branches | best effort |
| tagged releases | *support period not yet defined* |

The CRA requires a stated support period for products placed on the
market. This is a reference build, not a product, and Tropic Square has
not published one yet. The row above says so rather than implying one.

## Known limitations (not new findings)

These are documented and open; reports about them are welcome only if they add
something new. Details and plans are in the [README](README.md) ("Not done,
and said plainly") and the [CRA backlog](docs/cra/backlog.md).

- `sysupgrade` does not check who made an image (signed updates are in
  progress on a branch, not merged).
- No secure boot.
- Units that are not provisioned talk to their TROPIC01 with the published
  shared pairing key; on provisioned units it is restricted, not invalidated.
  The per-unit key's private half is on the router's flash.
- Only the `/vault` folder is encrypted.
- The security log is not tamper-evident.
- On a unit with no root password, the first person on the LAN can set it
  through the web UI.
- AWS IoT telemetry does not work.

## Software bill of materials

Every image build writes a CycloneDX SBOM next to the image
(`*.bom.cdx.json`), listing each package with its version and license.

## Secrets

This repository does not store credentials, private keys, per-unit
passwords, device certificates or VPN keys. The host tools that create them
refuse to write inside a git work tree, and CI runs a secret scan
(gitleaks) on every push. If you find any secret in the git history, report
it as above so it can be rotated.
