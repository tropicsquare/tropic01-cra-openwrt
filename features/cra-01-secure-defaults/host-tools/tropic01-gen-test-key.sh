#!/bin/sh
# tropic01-gen-test-key.sh -- DEV/BENCH ONLY: generate a throwaway SSH test
# key and bake its public half into the next image.
#
# This is NOT a production provisioning step (that is
# tropic01-gen-credential.sh). It exists so a developer -- or an assistant --
# can reach a freshly flashed bench unit over SSH without a human typing a
# password on the serial console after every flash. Every run creates a NEW
# ed25519 keypair; no key is ever fixed, shared or committed.
#
# Effects on the image built afterwards:
#   - files/etc/dropbear/authorized_keys holds the public key (the file
#     dropbear actually reads). 95-tropic01-secure-defaults then also turns
#     SSH password login off, because a key is present.
#   - files/etc/tropic01-dev-build is created. Its presence makes the
#     forced-password console hook skip itself. The image is deliberately
#     less strict than a default build and must never ship to anyone.
#
# Not wired into `make build`: a plain build trusts no key and keeps the
# forced-password hook. Run this first, deliberately, then `make build`.
#
# Usage (from the repo root):
#   ./features/cra-01-secure-defaults/host-tools/tropic01-gen-test-key.sh \
#       build/openwrt/files /secure/test-keys
#   make build
#   ssh -i /secure/test-keys/<printed key file> root@<router-ip>
#
# Writes:
#   <openwrt-files-dir>/etc/dropbear/authorized_keys  -- public key only
#   <openwrt-files-dir>/etc/tropic01-dev-build        -- dev-build marker
#   <key-out-dir>/tropic01-test-key-<stamp>[.pub]     -- keypair; the private
#       half stays here only. Keep it off git and off shared drives.

set -eu
umask 077

FILES_DIR="${1:?usage: $0 <openwrt-files-dir> <key-out-dir>}"
KEY_DIR="${2:?usage: $0 <openwrt-files-dir> <key-out-dir>}"

log() { echo "[tropic01-gen-test-key] $*" >&2; }
die() { log "ERROR: $*"; exit 1; }

command -v ssh-keygen >/dev/null 2>&1 || die "ssh-keygen not found"

# The private key must never end up in a commit; refuse any git work tree.
mkdir -p "$KEY_DIR"
if git -C "$KEY_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	die "key directory '$KEY_DIR' is inside a git work tree ($(git -C "$KEY_DIR" rev-parse --show-toplevel)) -- pick a location outside any repository"
fi

STAMP="$(date +%Y%m%d-%H%M%S)-$$"
KEY="$KEY_DIR/tropic01-test-key-$STAMP"
ssh-keygen -q -t ed25519 -N '' -C "tropic01-dev-test-key-$STAMP" -f "$KEY"

mkdir -p "$FILES_DIR/etc/dropbear"
# Overlay directory modes become the image's / and /etc modes; umask 077
# would make them root-only and the unit would not boot (ubusd runs as an
# unprivileged user). Keep them 755; only dropbear/ is private.
chmod 755 "$FILES_DIR" "$FILES_DIR/etc"
chmod 700 "$FILES_DIR/etc/dropbear"
cp "$KEY.pub" "$FILES_DIR/etc/dropbear/authorized_keys"
chmod 600 "$FILES_DIR/etc/dropbear/authorized_keys"

cat > "$FILES_DIR/etc/tropic01-dev-build" <<-'EOF'
	DEV/BENCH BUILD: skips the forced root-password prompt.
	Made by host-tools/tropic01-gen-test-key.sh. Never ship this image.
EOF
chmod 644 "$FILES_DIR/etc/tropic01-dev-build"

log "DEV/TEST key only -- regenerated per run, not a production credential"
log "public key baked into ${FILES_DIR}/etc/dropbear/authorized_keys"
log "dev-build marker written: ${FILES_DIR}/etc/tropic01-dev-build"
log "private key: $KEY (keep it out of git)"
