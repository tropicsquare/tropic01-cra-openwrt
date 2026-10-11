# core — rules for AI assistants

- Read the `.claude/skills/tropic01-pkcs11/` skill's "Known Pitfalls" section
  before diagnosing a TROPIC01/PKCS#11/SPI build or runtime error from
  scratch — it has the answer to most of the recurring ones.
- Never regenerate an ECC key or write to R-MEM slot 0 on real hardware
  without the user asking for that specific action: a hardware-generated
  key that is overwritten cannot be recovered. R-MEM slot 0 is not used:
  the documentation calls it read-protected on `ENGINEERING_SAMPLES_01`
  silicon, but on the bench chip it read back slot 1's data (backlog 3.14,
  [docs/cra/backlog.md](../../docs/cra/backlog.md)). Don't rely on either.
- The pairing-key patches (`003-use-engineering-sample-keys.patch`,
  `007-load-per-unit-pairing-key.patch`,
  `009-select-slot0-key-at-runtime.patch`) live here. Provisioning a
  per-unit key and changing access rights (UAP) belong in
  [cra-04-access-control](../cra-04-access-control/) (`tropic01-pairing`).
  Don't change either in passing: it changes how every unit authenticates
  to its chip, and needs its own hardware test.
- Pin every source (see root `CLAUDE.md`): `PKG_SOURCE_VERSION` is a commit
  hash, never `HEAD`, with a real `PKG_MIRROR_HASH`/`PKG_HASH`.
