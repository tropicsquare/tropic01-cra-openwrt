# Contributing

## Branch model

- `main` is always buildable and only contains features that were tested
  on hardware. It is protected: changes land through pull requests with
  passing CI.
- Each feature is developed on its own branch, named after its folder:
  `feat/cra-01-secure-defaults`, `feat/cra-04-access-control`, … Smaller
  follow-ups use `fix/<scope>-<topic>` or `docs/<topic>`.
- Keep branch names to `[a-z0-9/-]`. Characters like `#` break shells and URLs.

## What a feature needs before it merges into main

1. `make` builds the image from a clean checkout (CI `build` workflow).
2. `make lint` passes (CI `lint` workflow, which also runs a secret scan).
3. It was tested on an OpenWrt One + TROPIC01. The PR description says
   how, on which hardware revision, and what the output was.
4. `features/<name>/README.md` is up to date, **including what the feature
   does not fix**, and the status in [README.md](README.md) and
   [docs/cra/annex-i-mapping.md](docs/cra/annex-i-mapping.md) matches.
5. Documentation links resolve (`python3 scripts/check-doc-links.py`, also in CI),
   `make test` passes, and any item you leave open is in
   [docs/cra/backlog.md](docs/cra/backlog.md).
6. Any irreversible step (key-slot invalidation, eFuses, counters) is
   marked as such in the docs and the feature's `CLAUDE.md`.

## Adding a feature or package

```
features/<feature>/
  README.md          goal, design, status, what it does NOT fix, how to test
  CLAUDE.md          rules for AI assistants in this folder
  openwrt.config     config fragment (CONFIG_PACKAGE_<pkg>=y, ...)
  <package>/         one folder per OpenWrt package: Makefile, files/, patches/
  host-tools/        scripts that run on the build/provisioning host, if any
```

`features/cra-01-secure-defaults/` is the reference layout.

## Coding guidelines

- **Shell:** POSIX `sh`, `#!/bin/sh`, `set -eu` for host tools; router-side
  scripts log through `logger` with a `log()` helper and fail through
  `die()`. Must pass `shellcheck -s sh`.
- **C:** OpenWrt upstream style (K&R braces, tabs, 80 columns soft limit).
- **Package Makefiles:** OpenWrt conventions. Pin sources to a tag or
  commit hash, never `HEAD`, and set `PKG_HASH` / `PKG_MIRROR_HASH`.
- **Patches:** `git format-patch` style, named `NNN-verb-description.patch`,
  numbers unique per package.
- **OpenWrt itself:** only through `openwrt-patches/`, minimal hunks, with
  a header explaining why.

## Commit messages

[Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/),
scoped to the feature or package:

```
feat(cra-01): pin dropbear to lan and disable root password login
fix(libtropic-pkcs11): pin source to 3ddecde
docs(cra-04): document slot invalidation procedure
```

Sign off your commits (`git commit -s`).

## Secrets

Never commit private keys, per-unit passwords, credential manifests,
filled-in `.ovpn` profiles or AWS device certificates. `.gitignore` covers
the usual names, and CI runs gitleaks, but neither replaces checking your
diff. If something slipped through, report it as described in
[SECURITY.md](SECURITY.md).
