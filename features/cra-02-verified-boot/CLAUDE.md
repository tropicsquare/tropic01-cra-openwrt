# cra-02-verified-boot — rules for AI assistants

- Burning SoC eFuses (secure-boot enable, ROTPK hash) is **irreversible**
  and a mistake bricks the board. Never run, script or suggest running a
  fuse-programming command without the user explicitly confirming that
  exact step, on that exact board, in the current conversation.
- Never generate, commit or print production signing keys. Test keys go
  under a directory covered by `.gitignore`.
- Do not describe TROPIC01 as the boot root of trust (see README).
- Monotonic counter updates (each decrements by one) and `counter init`
  change the chip: confirm first. `counter init` can be repeated, so the
  design must keep that right away from any key an attacker can get.
