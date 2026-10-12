#!/bin/sh
# tropic01-update-keys.sh -- the keys behind cra-07's signed sysupgrade images, made with OpenWrt's usign and ucert.
#
#   tropic01-update-keys.sh new-root ROOTDIR
#       a new update root key pair: ROOTDIR/root.sec (keep it offline) and ROOTDIR/root.pub (goes into TROPIC01 R-MEM
#       slot 200 of every unit with: tropic01-update keys install 200 root.pub --yes-write-update-key)
#   tropic01-update-keys.sh issue ROOTDIR SIGNDIR
#       a new release (build) key in SIGNDIR: key-build, key-build.pub, and key-build.ucert, a certificate issued by the
#       root; plus key-build.ucert.revoke, the revoker certificate. Point the build at it: TROPIC01_SIGNING_DIR=SIGNDIR
#   tropic01-update-keys.sh show DIR
#       fingerprints and certificate contents; never secret material
#
# Every directory must be outside any git work tree, and existing keys are never overwritten. Secret keys are never
# printed. The tools come from an OpenWrt build tree ($OPENWRT_DIR/staging_dir/host/bin) or the PATH.

set -eu
umask 077

log() { echo "[tropic01-update-keys] $*" >&2; }
die() { log "ERROR: $*"; exit 1; }

HOSTBIN="${OPENWRT_DIR:-$(dirname "$0")/../../../build/openwrt}/staging_dir/host/bin"
[ -d "$HOSTBIN" ] && PATH="$HOSTBIN:$PATH"
for t in usign ucert; do command -v "$t" >/dev/null 2>&1 || die "$t not found (build OpenWrt's host tools, or set OPENWRT_DIR)"; done

# checked on the nearest existing directory, before anything is created
outside_git() {
	p="$1"
	while [ ! -d "$p" ]; do p="$(dirname "$p")"; done
	if git -C "$p" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
		die "$1 is inside a git work tree ($(git -C "$p" rev-parse --show-toplevel)); keep keys outside any repository"
	fi
	mkdir -p "$1"
}

no_clobber() { for f in "$@"; do [ ! -e "$f" ] || die "$f exists; refusing to overwrite a key"; done; }

case "${1:-}" in
new-root)
	d="${2:?usage: $0 new-root ROOTDIR}"
	outside_git "$d"
	no_clobber "$d/root.sec" "$d/root.pub"
	usign -G -s "$d/root.sec" -p "$d/root.pub" -c "tropic01 update root" || die "usign -G failed"
	log "update root key $(usign -F -p "$d/root.pub") in $d (root.sec stays offline; root.pub goes into TROPIC01 R-MEM slot 200)"
	;;
issue)
	r="${2:?usage: $0 issue ROOTDIR SIGNDIR}"
	d="${3:?usage: $0 issue ROOTDIR SIGNDIR}"
	[ -s "$r/root.sec" ] || die "no root key $r/root.sec"
	outside_git "$r"
	outside_git "$d"
	no_clobber "$d/key-build" "$d/key-build.pub" "$d/key-build.ucert" "$d/key-build.ucert.revoke"
	usign -G -s "$d/key-build" -p "$d/key-build.pub" -c "tropic01 release key" || die "usign -G failed"
	ucert -I -c "$d/key-build.ucert" -p "$d/key-build.pub" -s "$r/root.sec" || die "ucert -I failed"
	[ -s "$d/key-build.ucert" ] || die "ucert produced no certificate"
	log "release key $(usign -F -p "$d/key-build.pub") with a certificate from root $(usign -F -s "$r/root.sec") in $d"
	log "build with: make build TROPIC01_SIGNING_DIR=$d TROPIC01_IMAGE_SV=<n>"
	;;
show)
	d="${2:?usage: $0 show DIR}"
	[ -f "$d/root.pub" ] && log "root key: $(usign -F -p "$d/root.pub")"
	[ -f "$d/key-build.pub" ] && log "release key: $(usign -F -p "$d/key-build.pub")"
	[ -f "$d/key-build.ucert" ] && ucert -D -c "$d/key-build.ucert" >&2
	true
	;;
*) echo "usage: $0 new-root ROOTDIR | issue ROOTDIR SIGNDIR | show DIR" >&2; exit 2 ;;
esac
