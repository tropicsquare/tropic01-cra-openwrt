# CRA Annex I mapping

This repository takes an OpenWrt One router with a TROPIC01 secure element
and works through the EU Cyber Resilience Act (CRA) Annex I essential
requirements one at a time. For each requirement it asks the same
question: **what can hardware actually fix, and what is policy that no
chip can fix for you?**

That split matters. A secure element makes some requirements genuinely
stronger (keys that cannot be copied off the device, a per-unit identity).
It contributes nothing to others (which ports are open). Claiming otherwise
is how "secure by design" turns into "secure by checklist".

## Status overview

| Feature | Annex I, Part I | What it covers | Status |
|---|---|---|---|
| [cra-01-secure-defaults](../../features/cra-01-secure-defaults/) | 2(b) | secure-by-default configuration, reset to original state | partial: attack-surface baseline + per-unit credential; reset integrity not started |
| [cra-02-verified-boot](../../features/cra-02-verified-boot/) | 2(f), 2(c) | firmware integrity, rollback protection for security updates | not started |
| [cra-03-key-storage](../../features/cra-03-key-storage/) | 2(e) | confidentiality of stored and transmitted data | done for OpenVPN: the key is generated and used inside TROPIC01, verified on hardware including the boot-time start; AWS IoT telemetry is moved to the backlog (3.1) and not claimed |
| [cra-04-access-control](../../features/cra-04-access-control/) | 2(d) | authentication, identity, access control | partial: per-device identity done; pairing keys are shared fleet-wide, no UAP scoping |
| [cra-05-secure-erase](../../features/cra-05-secure-erase/) | 2(m) | secure removal of user data and settings | partial: a small encrypted vault with a TROPIC01-held key and crypto-erase (verified on hardware); the rest of the overlay is not encrypted |
| [cra-06-security-logging](../../features/cra-06-security-logging/) | 2(l) | security logging with a user opt-out | partial: event log, change audit and opt-out; not tamper-proof, no TROPIC01 involvement |

What is still open, feature by feature, is in [backlog.md](backlog.md).

## Role of TROPIC01 per feature

| Feature | Role of TROPIC01 |
|---|---|
| core | the connection: SPI session, PKCS#11 token, `tropicctl`; no security property of its own |
| cra-01 | none today (policy and configuration) |
| cra-02 | **not** the boot root of trust (that is the SoC BootROM and its eFuses); possible monotonic counter for rollback protection, not implemented |
| cra-03 | central: generates and holds the VPN private key, signs, stores the certificate in R-MEM |
| cra-04 | central, unfinished: pairing keys and access policy decide who may talk to the chip |
| cra-05 | central: random vault key, key storage in R-MEM slot 100, crypto-erase by erasing that slot |
| cra-06 | none today; possible future anchoring of log hashes |

`core/` is not a CRA feature: it is the TROPIC01 platform layer (libtropic,
the PKCS#11 module, `tropicctl`) that the features build on.

## Per requirement: hardware vs. policy

### 2(b) Secure by default, with reset to original state

Three different things hide in this one sentence:

| Sub-requirement | Can hardware help? |
|---|---|
| Minimal attack surface (no open WAN services) | **No.** Firewall and image policy, full stop. |
| Credential quality (no guessable default password) | Only as an entropy source. `/dev/urandom` is already good enough; the real fix is *per-unit* generation. |
| Reset integrity (reset restores a *verified* baseline) | **Yes**, but only on top of a verified boot chain (2(f)). |

### 2(f) + 2(c) Integrity and rollback protection

The immutable root of trust on MT7981 is the SoC boot ROM verifying a key
fused into the SoC (BL2 → FIP → U-Boot → FIT image). **TROPIC01 does not
anchor the boot chain** and this repo does not claim it does. What
TROPIC01 can honestly add is a monotonic counter for anti-rollback, checked
before an older signed image is accepted.

### 2(e) Confidentiality

This is where a secure element is strongest: private keys are generated
inside TROPIC01 and never leave it, so dumping the flash does not yield the
device's TLS or VPN identity.

### 2(d) Authentication and access control

Per-device identity (hardware key + X.509 certificate in TROPIC01 R-MEM) is
in place. Host-to-chip access is not yet per-unit: every build uses the
same published engineering-sample pairing key, and there is no User Access
Privileges (UAP) scoping. That is the "checklist" pattern the project
argues against, and cra-04 exists to fix it.

### 2(m) Secure data removal

Crypto-erase means destroying a key that encrypts the data. Today one
encrypted vault (`/vault`) exists, with its key stored in TROPIC01 and erased
on `destroy`. The rest of the overlay filesystem is not encrypted, so a
factory reset still erases it the ordinary way. Encrypting everything is in the
[backlog](backlog.md) (5.4).

## Not covered by the features above

- **Annex I, Part II (vulnerability handling).** Images ship a CycloneDX
  SBOM (`CONFIG_JSON_CYCLONEDX_SBOM`). Disclosure is in
  [SECURITY.md](../../SECURITY.md). A published support period is still
  missing: that is a Tropic Square decision, not a code change.
- **2(c) update delivery / notification** is not addressed yet. 2(l) security
  logging is covered in part by cra-06.

## Compliance vs. development velocity

Some of the requirements above are individually correct and also make
fast bench-testing iteration slower (a unique per-unit credential means
no more "one image, flash any unit, SSH in"; a per-unit TROPIC01 pairing
key means a fresh dev unit's chip doesn't just work). That tension is
real and isn't a reason to skip the requirement — see
[compliance-pitfalls.md](compliance-pitfalls.md) for the specific
trade-offs this repo has hit and what it did (or deliberately didn't do)
about each one.
