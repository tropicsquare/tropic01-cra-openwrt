# core — rules for AI assistants

- Read the `.claude/skills/tropic01-pkcs11/` skill's "Known Pitfalls" section
  before diagnosing a TROPIC01/PKCS#11/SPI build or runtime error from
  scratch — it has the answer to most of the recurring ones.
- Never regenerate an ECC key or write to R-MEM slot 0 on real hardware
  without the user asking for that specific action: a hardware-generated
  key that is overwritten cannot be recovered, and R-MEM slot 0 is
  read-protected on `ENGINEERING_SAMPLES_01` silicon (writing it fails, but
  don't assume that holds for all silicon).
- The shared pairing key (`003-use-engineering-sample-keys.patch`) and any
  per-unit pairing-key or UAP work belongs in
  [cra-04-access-control](../cra-04-access-control/), not here. Don't fix
  it "while you're in the area" — it changes how every unit authenticates
  to its chip, and needs its own hardware test.
- Pin every source (see root `CLAUDE.md`): `PKG_SOURCE_VERSION` is a commit
  hash, never `HEAD`, with a real `PKG_MIRROR_HASH`/`PKG_HASH`.
