# cra-04-access-control — rules for AI assistants

- Invalidating a pairing key slot is **irreversible**. Invalidating slot 0
  before a working per-unit key is verified locks you out of the chip for
  good. Never run or script it without explicit user confirmation for that
  step, on that unit, in the current conversation.
- Never commit per-unit pairing keys or print private halves.
- Test UAP changes on a spare chip first and say so in the PR.
- The per-unit key files (`/etc/tropic01/pairing.key`) are credentials like
  the cra-01 password hash: never print, log or commit them, and keep the
  overlay directory modes at 0755 (only the key file is 0600), or the image
  will not boot.
- Writing a pairing slot is also a one-way step (a slot is consumed and the
  write goes to I-Memory, which can fail silently on old firmware): treat
  it like invalidation and read the slot back. Same rule: explicit
  confirmation, on that unit, in the current conversation.
