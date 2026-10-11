#!/bin/sh
# prepare.sh -- turn a pinned upstream OpenWrt checkout into a tree that
# builds this repo's features.
#
# Steps (each one is idempotent, so re-running is always safe):
#   1. clone upstream OpenWrt into $OPENWRT_DIR and check out the commit
#      pinned in openwrt.pin
#   2. apply openwrt-patches/*.patch (the only changes to OpenWrt itself)
#   3. write feeds.conf: this repo's feed first, then the pinned upstream
#      feeds, then update + install them
#   4. assemble .config from configs/openwrt-one.config plus every
#      features/*/openwrt.config fragment, and expand it with defconfig
#   5. image signing (cra-07), only when asked: TROPIC01_SIGNING_DIR holds
#      key-build and key-build.ucert (outside any git work tree), and
#      TROPIC01_IMAGE_SV is the image's security version
#
# $OPENWRT_DIR is generated state. Local edits to tracked files in it are
# discarded whenever the patch set changes -- work in this repo instead.

set -eu

TOP="$(cd "$(dirname "$0")/.." && pwd)"
OPENWRT_DIR="${OPENWRT_DIR:-$TOP/build/openwrt}"
DL_DIR="${DL_DIR:-}"
CONFIG_BASE="${CONFIG_BASE:-$TOP/configs/openwrt-one.config}"

log() { echo "[prepare] $*" >&2; }
die() { echo "[prepare] ERROR: $*" >&2; exit 1; }

# shellcheck source=/dev/null
. "$TOP/openwrt.pin"
[ -n "${OPENWRT_URL:-}" ] && [ -n "${OPENWRT_COMMIT:-}" ] ||
	die "openwrt.pin must set OPENWRT_URL and OPENWRT_COMMIT"

checkout_openwrt() {
	if [ ! -d "$OPENWRT_DIR/.git" ]; then
		log "cloning $OPENWRT_URL into $OPENWRT_DIR"
		mkdir -p "$(dirname "$OPENWRT_DIR")"
		git clone -q "$OPENWRT_URL" "$OPENWRT_DIR"
	fi

	if [ "$(git -C "$OPENWRT_DIR" rev-parse HEAD)" != "$OPENWRT_COMMIT" ]; then
		git -C "$OPENWRT_DIR" cat-file -e "$OPENWRT_COMMIT^{commit}" 2>/dev/null ||
			git -C "$OPENWRT_DIR" fetch -q origin
		log "checking out pinned OpenWrt $OPENWRT_COMMIT"
		# Tracked files only carry our own patches; reset them first.
		git -C "$OPENWRT_DIR" checkout -q -- .
		git -C "$OPENWRT_DIR" checkout -q --detach "$OPENWRT_COMMIT"
		rm -f "$OPENWRT_DIR/.tropic01-patches"
	fi
}

apply_patches() {
	stamp="$OPENWRT_DIR/.tropic01-patches"
	want="$(cat "$TOP"/openwrt-patches/*.patch 2>/dev/null | sha256sum | cut -d' ' -f1)"
	[ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ] && return 0

	log "applying openwrt-patches/"
	git -C "$OPENWRT_DIR" checkout -q -- .
	# checkout does not remove files a previous run created (a patch that
	# adds a new file, e.g. a busybox patch), and re-applying would then fail
	# with "already exists in working directory".
	for p in "$TOP"/openwrt-patches/*.patch; do
		[ -e "$p" ] || continue
		git apply --summary "$p" | sed -n 's/^ create mode [0-9]* //p' |
			while IFS= read -r f; do rm -f "${OPENWRT_DIR:?}/$f"; done
	done
	for p in "$TOP"/openwrt-patches/*.patch; do
		[ -e "$p" ] || continue
		log "  $(basename "$p")"
		git -C "$OPENWRT_DIR" apply "$p" || die "patch failed: $p"
	done
	echo "$want" > "$stamp"
}

setup_dl_dir() {
	[ -n "$DL_DIR" ] || return 0
	mkdir -p "$DL_DIR"
	if [ -L "$OPENWRT_DIR/dl" ] || [ ! -e "$OPENWRT_DIR/dl" ]; then
		ln -sfn "$DL_DIR" "$OPENWRT_DIR/dl"
		log "using shared download cache $DL_DIR"
	else
		log "$OPENWRT_DIR/dl already exists as a directory; not replacing it with DL_DIR"
	fi
}

setup_feeds() {
	{
		echo "src-link tropic01 $TOP/features"
		grep -v '^[[:space:]]*#' "$TOP/feeds.conf" | grep -v '^[[:space:]]*$'
	} > "$OPENWRT_DIR/feeds.conf"

	log "updating feeds"
	(cd "$OPENWRT_DIR" && ./scripts/feeds update -a >/dev/null)

	# scripts/feeds treats a package as "already installed" from whichever
	# feed symlinked it first, and its own -f only overrides in-tree core
	# packages -- it does NOT move a package from one feed to another, even
	# with -p tropic01. So a package that also exists upstream (openvpn,
	# opensc) stays pinned to whatever feed happened to install it first,
	# on every later run, unless we clear that stale symlink ourselves
	# before installing. Do that for every directory name this repo's feed
	# provides, in every *other* feed.
	for pkgdir in "$TOP"/features/*/*/; do
		[ -d "$pkgdir" ] || continue
		name="$(basename "$pkgdir")"
		for link in "$OPENWRT_DIR"/package/feeds/*/"$name"; do
			[ -L "$link" ] || continue
			case "$link" in */feeds/tropic01/*) continue ;; esac
			log "  clearing stale $name symlink: ${link#"$OPENWRT_DIR"/}"
			rm -f "$link"
		done
	done

	# Our feed first: packages that also exist upstream (openvpn, opensc)
	# must resolve to this repo's version.
	(cd "$OPENWRT_DIR" && ./scripts/feeds install -a -p tropic01 >/dev/null)
	(cd "$OPENWRT_DIR" && ./scripts/feeds install -a >/dev/null)
}

write_config() {
	log "assembling .config from $(basename "$CONFIG_BASE") + feature fragments"
	{
		cat "$CONFIG_BASE"
		for frag in "$TOP"/features/*/openwrt.config; do
			[ -e "$frag" ] || continue
			echo "# --- ${frag#"$TOP"/} ---"
			cat "$frag"
		done
	} > "$OPENWRT_DIR/.config"
	(cd "$OPENWRT_DIR" && make defconfig >/dev/null)
}

# cra-07: OpenWrt's append-metadata signs every image when $OPENWRT_DIR/key-build and key-build.ucert exist, and patch
# 0003 puts TROPIC01_IMAGE_SV into the signed metadata. The same value goes into the root filesystem, where the router
# reads the running image's security version. Both are optional. Whatever a previous run placed is removed first, so a
# build without them is really unsigned and carries no security version.
setup_update_signing() {
	marker="$OPENWRT_DIR/.tropic01-signing"
	sv_file="$OPENWRT_DIR/files/usr/share/tropic01/image-sv"
	if [ -e "$OPENWRT_DIR/key-build" ] && [ ! -e "$marker" ]; then
		die "$OPENWRT_DIR/key-build exists but was not placed by this harness; remove it or move it to TROPIC01_SIGNING_DIR"
	fi
	rm -f "$OPENWRT_DIR/key-build" "$OPENWRT_DIR/key-build.ucert" "$marker" "$sv_file"

	if [ -n "${TROPIC01_SIGNING_DIR:-}" ]; then
		d="$TROPIC01_SIGNING_DIR"
		[ -d "$d" ] || die "TROPIC01_SIGNING_DIR=$d is not a directory"
		if git -C "$d" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
			die "TROPIC01_SIGNING_DIR=$d is inside a git work tree; keep signing keys outside any repository"
		fi
		[ -s "$d/key-build" ] && [ -s "$d/key-build.ucert" ] ||
			die "TROPIC01_SIGNING_DIR=$d must hold key-build and key-build.ucert (make them with features/cra-07-secure-update/host-tools/tropic01-update-keys.sh)"
		touch "$marker"
		install -m 600 "$d/key-build" "$OPENWRT_DIR/key-build"
		install -m 644 "$d/key-build.ucert" "$OPENWRT_DIR/key-build.ucert"
		log "image signing: on (release key and certificate from $d)"
	else
		log "image signing: off (TROPIC01_SIGNING_DIR not set): the image is unsigned"
	fi

	if [ -n "${TROPIC01_IMAGE_SV:-}" ]; then
		case "$TROPIC01_IMAGE_SV" in *[!0-9]*) die "TROPIC01_IMAGE_SV must be a whole number (got '$TROPIC01_IMAGE_SV')" ;; esac
		[ "$TROPIC01_IMAGE_SV" -ge 1 ] && [ "$TROPIC01_IMAGE_SV" -le 4294967294 ] ||
			die "TROPIC01_IMAGE_SV must be 1 to 4294967294"
		mkdir -p "$(dirname "$sv_file")"
		echo "$TROPIC01_IMAGE_SV" > "$sv_file"
		log "image security version: $TROPIC01_IMAGE_SV (signed metadata and /usr/share/tropic01/image-sv)"
	fi
	if [ -n "${TROPIC01_SIGNING_DIR:-}" ] && [ -z "${TROPIC01_IMAGE_SV:-}" ]; then
		log "WARNING: signed but without TROPIC01_IMAGE_SV: a router running cra-07 refuses an image without a security version"
	elif [ -z "${TROPIC01_SIGNING_DIR:-}" ] && [ -n "${TROPIC01_IMAGE_SV:-}" ]; then
		log "WARNING: a security version but no signing: a router running cra-07 refuses an unsigned image"
	fi
}

checkout_openwrt
apply_patches
setup_dl_dir
setup_feeds
write_config
setup_update_signing
log "ready: $OPENWRT_DIR"
