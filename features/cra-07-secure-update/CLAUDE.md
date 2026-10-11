# cra-07-secure-update — rules for AI assistants

- This feature is the update path only. Secure boot is cra-02. Never describe this as secure boot, and never say TROPIC01
  verifies signatures: it stores the root keys and the counter; `ucert` verifies.
- Initialising the rollback counter (`tropic01-update floor init`, `tropicctl counter init`) and every counter update are
  chip writes that cannot be undone in the end state: explicit confirmation from the user, for that unit, every time.
  A counter update loop must be bounded by the exact number of steps; a runaway loop burns the counter's range.
- Writing the root key slots (200, 201) is a chip write: confirm first, and never touch other R-MEM slots.
- Never commit or print private signing keys (`*.sec`, `key-build`), certificates are fine to show but are not committed
  either; keys live outside the repository.
- Keep the README's "what this does not fix" list accurate when anything changes.
