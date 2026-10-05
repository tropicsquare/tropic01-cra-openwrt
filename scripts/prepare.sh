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

checkout_openwrt
apply_patches
setup_dl_dir
setup_feeds
write_config
log "ready: $OPENWRT_DIR"
