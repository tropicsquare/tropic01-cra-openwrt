# cra-03-key-storage — rules for AI assistants

- Never commit a filled-in OpenVPN profile (`*.ovpn`), a device
  certificate, an AWS IoT endpoint, or anything under `examples/` that
  isn't the `.example` template. `.gitignore` covers the common patterns
  but doesn't replace checking the diff yourself.
- Every device must have a **unique CN** in its client certificate — the
  OpenVPN server assigns VPN IPs based on CN, and AWS IoT device identity
  depends on it too. Never reuse a CN across units, including in examples
  or test fixtures.
- OpenVPN's version is pinned deliberately (see README "What this does NOT
  fix"). Don't bump it as a drive-by fix.
- `host-tools/tropic01-install-ovpn.sh` reads a filled-in profile and writes its CA and TLS key to a router.
  It must never print, log or echo anything from the profile (including the `remote` host). When you test it,
  redact addresses from its output and delete temporary copies; the files it installs are `0600`.
- Say in the user guide which steps were verified on hardware and which were not. Steps that write the chip
  (generating a key, storing a certificate) are irreversible for that slot: confirm with the user first.
