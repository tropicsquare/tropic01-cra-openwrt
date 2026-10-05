# cra-01-secure-defaults — rules for AI assistants

- Never print or commit the contents of a credential manifest CSV, or any
  plaintext password it contains. The generator script itself refuses to
  write a manifest inside a git work tree — don't work around that
  refusal, it's there on purpose.
- Never run `host-tools/tropic01-gen-credential.sh` against a real batch
  of units without the user asking for that specific run. It's safe to
  run against a scratch directory for testing.
- Never print or commit the private half of a key made by
  `host-tools/tropic01-gen-test-key.sh`. It is a bench-testing convenience
  only: regenerate per build, never treat the resulting image as shippable
  (it carries `/etc/tropic01-dev-build`, which disables the forced-password
  hook), and never wire it into `make build`.
- Host tools run under `umask 077`, and ImageBuilder copies the overlay's
  *directory* modes onto the image's `/` and `/etc`. Any script that
  creates `build/openwrt/files/...` must `chmod 755` those directories
  (only the files inside stay private). A 0700 `/` makes `ubusd` (run as
  user `ubus`) fail with exit 127 and the unit never reaches userspace.
  Deleting `files/etc/shadow` does not undo it -- check `stat` on the dirs.
- Don't claim Annex I 2(b) ("secure by default") is done. See README's
  scope table: attack surface is policy no chip fixes, and reset integrity
  isn't implemented. Only credential quality is a real, hardware-adjacent
  improvement, and even that depends on the entropy source actually being
  good (today: `/dev/urandom`, which is fine, but say so rather than
  implying TROPIC01 involvement that isn't there).
- Follow this feature's package structure as the template for new
  packages in this repo (see root `CONTRIBUTING.md`).
