#!/bin/sh
# tropic01-gen-credential.sh -- CRA Annex I(b): generate a per-unit default
# root credential at provisioning time.
#
# Run this on the BUILD HOST, before flashing a unit -- never on the
# router, and never at first boot. Rationale: a first-boot script has no
# entropy source a build-host script doesn't already have, and generating
# the credential at first boot would mean overwriting a password the owner
# already changed on every subsequent boot.
# This runs once per unit, at image/provisioning time; the plaintext goes
# on that unit's physical label -- never onto the device, never into git.
#
# Entropy source:
#   /dev/urandom (the kernel CSPRNG) -- 128 bits. Already a large
#   improvement over any serial-number-derived formula, and does not
#   require TROPIC01 or any secure element to be a real fix for this
#   sub-requirement.
#
#   TROPIC01-sourced entropy is NOT implemented in this script. Wiring an
#   actual raw-random pull from the chip needs a host-native
#   lt-util/libtropic build talking to it over USB or a host SPI adapter
#   -- see https://github.com/tropicsquare/libtropic. This script does not
#   silently claim to use it: every run logs plainly that the source is
#   /dev/urandom.
#
# Usage:
#   ./tropic01-gen-credential.sh <unit-serial> <openwrt-build-dir> <manifest-csv>
#
# Example, from the repo root, against this harness's build tree:
#   ./features/cra-01-secure-defaults/host-tools/tropic01-gen-credential.sh \
#       SN00123 build/openwrt/files /secure/batch-07.csv
#   make build   # files/etc/shadow now bakes into this unit's image
#
# Writes:
#   <openwrt-build-dir>/files/etc/shadow -- full shadow file, root's
#                                     password HASH only (sha512-crypt)
#   <manifest-csv>                 -- appends "<unit-serial>,<plaintext>" for
#                                     the printed label. Operator-only: keep
#                                     it off git and off shared drives, and
#                                     delete it once labels are printed.

set -eu
umask 077

SERIAL="${1:?usage: $0 <unit-serial> <openwrt-build-dir> <manifest-csv>}"
FILES_DIR="${2:?usage: $0 <unit-serial> <openwrt-build-dir> <manifest-csv>}/etc"
MANIFEST="${3:?usage: $0 <unit-serial> <openwrt-build-dir> <manifest-csv>}"

log() { echo "[tropic01-gen-credential] $*" >&2; }
die() { log "ERROR: $*"; exit 1; }

# Refuse a manifest path inside a git work tree: it is the one file this
# script writes that must never end up in a commit, and putting it under
# version control at all invites exactly that mistake.
manifest_dir="$(dirname "$MANIFEST")"
mkdir -p "$manifest_dir"
if git -C "$manifest_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	die "manifest path '$MANIFEST' is inside a git work tree ($(git -C "$manifest_dir" rev-parse --show-toplevel)) -- pick a location outside any repository"
fi

gen_random_hex() {
	log "sourcing entropy from /dev/urandom (TROPIC01-sourced entropy not implemented, see header)"
	head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n'
}

# Base shadow file: every account base-files ships, so this script's
# output never drops the non-root accounts (daemon/ftp/network/nobody).
# Prefer the pinned OpenWrt tree's actual copy; fall back to an embedded
# copy of the same five lines (as of base-files in the pin this repo
# uses) if that tree hasn't been prepared yet.
BASE_SHADOW="$(dirname "$0")/../../../build/openwrt/package/base-files/files/etc/shadow"

PLAINTEXT="$(gen_random_hex)"
[ -n "$PLAINTEXT" ] || die "entropy source produced nothing"

# Hash via stdin, not an argv, so the plaintext never appears in this
# process's command line (and so in /proc/*/cmdline for other users on
# the build host to read).
HASH="$(printf '%s\n' "$PLAINTEXT" | openssl passwd -6 -stdin)"

mkdir -p "$FILES_DIR"
# The overlay's directory modes are copied onto the image's / and /etc, and
# umask 077 above would make them root-only -- unprivileged daemons such as
# ubusd (run as user ubus) then cannot even traverse / and the unit never
# boots to userspace. Only the files inside stay private.
chmod 755 "$FILES_DIR" "$(dirname "$FILES_DIR")"
if [ -f "$BASE_SHADOW" ]; then
	cp "$BASE_SHADOW" "$FILES_DIR/shadow.new"
elif [ -f "$FILES_DIR/shadow" ]; then
	cp "$FILES_DIR/shadow" "$FILES_DIR/shadow.new"
else
	log "WARNING: base-files shadow not found at $BASE_SHADOW (run 'make prepare' first) -- seeding from an embedded copy"
	cat > "$FILES_DIR/shadow.new" <<-'EOF'
	root:::0:99999:7:::
	daemon:*:0:0:99999:7:::
	ftp:*:0:0:99999:7:::
	network:*:0:0:99999:7:::
	nobody:*:0:0:99999:7:::
	EOF
fi
awk -v h="$HASH" -F: 'BEGIN{OFS=":"} $1=="root"{$2=h} {print}' \
	"$FILES_DIR/shadow.new" > "$FILES_DIR/shadow"
rm -f "$FILES_DIR/shadow.new"
chmod 600 "$FILES_DIR/shadow"

echo "${SERIAL},${PLAINTEXT}" >> "$MANIFEST"
chmod 600 "$MANIFEST"

log "wrote password hash for unit ${SERIAL} into ${FILES_DIR}/shadow"
log "plaintext appended to ${MANIFEST} -- print the label, then delete that line"
