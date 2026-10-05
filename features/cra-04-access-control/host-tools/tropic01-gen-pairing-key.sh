#!/bin/sh
# tropic01-gen-pairing-key.sh -- generate a per-unit TROPIC01 pairing key.
#
# CRA Annex I 2(d): today every build talks to the chip with the same
# published engineering-sample pairing key, so anyone holding that key and
# bus access can open a secure session to any unit. This creates the
# unit's own X25519 host key. It is the *host-side* half only: nothing here
# touches a chip. Writing the public half into a pairing slot, and
# invalidating the shared slot 0 key afterwards, are separate, irreversible
# provisioning steps that need an explicit decision per unit.
#
# Usage (from the repo root, once per unit, before building its image):
#   ./features/cra-04-access-control/host-tools/tropic01-gen-pairing-key.sh \
#       SN00123 build/openwrt/files /secure/manifests/pairing-batch-07.csv
#   make build
#
# Writes into <build-dir> (the OpenWrt `files/` overlay):
#   etc/tropic01/pairing.key   32 raw bytes, private X25519 scalar, mode 0600
#   etc/tropic01/pairing.pub   32 raw bytes, public key, mode 0644
#   etc/tropic01/pairing.slot  pairing-key slot this key is meant for ("1")
# and appends "serial,slot,public-key-hex,timestamp" to the manifest. The
# manifest holds only public values, but it is a unit inventory: keep it
# off git (the script refuses a path inside a work tree).
#
# The private half is never printed or logged. Entropy is OpenSSL's CSPRNG
# seeded from the kernel; TROPIC01-sourced entropy is not used here.
#
# Where the private half then lives is a real limit, not a detail: it sits
# on the router's flash in cleartext (root-only). That removes the *shared*
# fleet key -- compromising one unit no longer opens every other -- but it
# does not stop someone with a flash dump of *this* unit from opening a
# session to *this* chip. See features/cra-04-access-control/README.md.

set -eu
umask 077

USAGE="usage: $0 <unit-serial> <openwrt-files-dir> <manifest-csv>"
SERIAL="${1:?$USAGE}"
FILES_DIR="${2:?$USAGE}"
MANIFEST="${3:?$USAGE}"
SLOT=1

log() { echo "[tropic01-gen-pairing-key] $*" >&2; }
die() { log "ERROR: $*"; exit 1; }

command -v openssl >/dev/null 2>&1 || die "openssl not found"
case "$SERIAL" in
*[!A-Za-z0-9._-]* | "") die "unit serial may only contain letters, digits, '.', '_' and '-'" ;;
esac

manifest_dir="$(dirname "$MANIFEST")"
mkdir -p "$manifest_dir"
if git -C "$manifest_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	die "manifest path '$MANIFEST' is inside a git work tree ($(git -C "$manifest_dir" rev-parse --show-toplevel)) -- pick a location outside any repository"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

openssl genpkey -algorithm X25519 -out "$TMP/k.pem" 2>/dev/null || die "key generation failed"
# RFC 8410: the raw 32-byte key is the tail of the PKCS#8 / SPKI DER encoding.
openssl pkey -in "$TMP/k.pem" -outform DER 2>/dev/null | tail -c 32 > "$TMP/priv"
openssl pkey -in "$TMP/k.pem" -pubout -outform DER 2>/dev/null | tail -c 32 > "$TMP/pub"
[ "$(wc -c < "$TMP/priv")" -eq 32 ] && [ "$(wc -c < "$TMP/pub")" -eq 32 ] || die "unexpected key size from openssl"

# Self-check: rebuild a PKCS#8 key from the raw private bytes alone and
# confirm it yields the same public key -- i.e. the two files we ship match.
{ printf '\060\056\002\001\000\060\005\006\003\053\145\156\004\042\004\040'; cat "$TMP/priv"; } > "$TMP/rebuilt.der"
openssl pkey -inform DER -in "$TMP/rebuilt.der" -pubout -outform DER 2>/dev/null | tail -c 32 > "$TMP/pub2"
cmp -s "$TMP/pub" "$TMP/pub2" || die "public key does not match the private key (self-check failed)"

KEYDIR="$FILES_DIR/etc/tropic01"
if [ -e "$KEYDIR/pairing.key" ]; then
	log "WARNING: replacing an existing pairing key in $KEYDIR. If that key was written to a chip, its private half exists only in the image built from it."
fi
mkdir -p "$KEYDIR"
# The overlay's directory modes become the image's: umask 077 would make /
# and /etc root-only and the unit would not boot (see cra-01 CLAUDE.md).
chmod 755 "$FILES_DIR" "$FILES_DIR/etc" "$KEYDIR"
cp "$TMP/priv" "$KEYDIR/pairing.key"
cp "$TMP/pub" "$KEYDIR/pairing.pub"
echo "$SLOT" > "$KEYDIR/pairing.slot"
chmod 600 "$KEYDIR/pairing.key"
chmod 644 "$KEYDIR/pairing.pub" "$KEYDIR/pairing.slot"

PUBHEX="$(od -An -tx1 "$TMP/pub" | tr -d ' \n')"
echo "${SERIAL},${SLOT},${PUBHEX},$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$MANIFEST"
chmod 600 "$MANIFEST"

log "unit ${SERIAL}: key pair written to ${KEYDIR} (private half not shown)"
log "public key (slot ${SLOT}): ${PUBHEX}"
log "NOTHING was written to a chip. Writing slot ${SLOT} and invalidating slot 0 are separate, irreversible steps."
