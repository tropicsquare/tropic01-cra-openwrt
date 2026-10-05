# Security policy

## Reporting a vulnerability

Please **do not** report security vulnerabilities through public GitHub
issues. E-mail:

> **security@tropicsquare.com**

Include as much as you can: type of issue (e.g. key leakage, privilege
escalation, RCE), affected component and version, steps to reproduce or a
proof of concept, and the potential impact.

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

## Software bill of materials

Every image build writes a CycloneDX SBOM next to the image
(`*.bom.cdx.json`), listing each package with its version and license.

## Secrets

This repository does not store credentials, private keys, per-unit
passwords, device certificates or VPN keys. If you find any in the git
history, report it as above so it can be rotated.
