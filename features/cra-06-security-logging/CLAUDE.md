# cra-06-security-logging — rules for AI assistants

- Never log secrets: no passwords, keys, hashes or file contents. The audit
  records file *names* and event kinds only. If you add a log source, check
  what it prints before putting it in the filter.
- The opt-out is a requirement of CRA 2(l), not a nicety. Keep
  `tropic01-security-log disable/enable/clear` working, and keep nothing
  leaving the device by default.
- Do not describe this as tamper-proof, as audit-grade evidence, or as using
  TROPIC01. It is none of those. The README's "does not fix" list stays
  accurate.
- Long-running scripts must exit on SIGTERM (`trap 'exit 0' INT TERM`); a
  trap that only cleans up leaves the service unkillable by procd.
- Keep the sandbox test passing (watch list, baseline, offline changes).
