#!/bin/sh
# gen-docs.sh <tropicctl-binary> <output-dir>: write the generated reference and manual page.
# The documentation is printed from the command table, so it cannot drift from the code.
set -eu
BIN="${1:?usage: gen-docs.sh <tropicctl-binary> <output-dir>}"
OUT="${2:?usage: gen-docs.sh <tropicctl-binary> <output-dir>}"
mkdir -p "$OUT"
"$BIN" help --format markdown > "$OUT/reference.md"
"$BIN" help --format man > "$OUT/tropicctl.1"
