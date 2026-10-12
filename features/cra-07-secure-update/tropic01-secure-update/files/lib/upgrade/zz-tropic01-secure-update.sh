# Sourced by validate_firmware_image after fwtool.sh (files are sourced in name order), so this definition of
# fwtool_check_signature replaces OpenWrt's. OpenWrt's version accepts an unsigned image unless REQUIRE_IMAGE_SIGNATURE=1
# and only marks a bad one "invalid", which sysupgrade -F overrides. This one asks tropic01-update and marks a refused image
# "broken": sysupgrade -F cannot install it.
fwtool_check_signature() {
	[ $# -gt 1 ] && return 1
	/usr/sbin/tropic01-update check "$1" && return 0
	command -v notify_firmware_broken >/dev/null 2>&1 && notify_firmware_broken
	return 1
}
