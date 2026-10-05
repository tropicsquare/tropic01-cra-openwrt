#!/bin/sh
# 95-tropic01-force-password.sh -- CRA Annex I 2(b): no interactive shell
# while root still has no password.
#
# Sourced by /etc/profile for every interactive login shell (serial console
# via login.sh's `login -f root`, and SSH sessions that got in by key).
# dropbear already refuses a blank-password login over the network, so this
# is what closes the gap for the console, where stock OpenWrt skips auth.
#
# "No password" includes a password of "" stored as a real hash, which
# /usr/libexec/tropic01-console-guard detects through rpcd. The guard also
# turns on console login (system.ttylogin) once a password exists and removes
# the blank-password warnings; it runs here and at every boot.
#
# Skipped for: non-tty sessions (must not hang `ssh host cmd`), and builds
# carrying /etc/tropic01-dev-build (written by host-tools/tropic01-gen-test-key.sh
# -- a deliberate, loudly named bench-testing opt-out; a plain build never has it).

GUARD=/usr/libexec/tropic01-console-guard

# shellcheck disable=SC3043  # 'local' is supported by OpenWrt's busybox ash
tropic01_force_password() {
	local state tries=0

	[ -t 0 ] || return 0
	[ -f /etc/tropic01-dev-build ] && return 0
	[ -x "$GUARD" ] || return 0

	state="$("$GUARD" state)"
	if [ "$state" = empty ]; then
		echo "Root's password is empty; clearing it so a real one can be set."
		passwd -d root >/dev/null 2>&1
		state=blank
	fi

	if [ "$state" = blank ]; then
		cat <<-'BANNER'

		 =====================================================================
		  This unit has NO ROOT PASSWORD (CRA Annex I 2(b)). You must set one
		  before you can use this shell.
		 =====================================================================

		BANNER

		# Ctrl-C must not be a way past the prompt.
		trap '' INT
		while [ "$state" = blank ]; do
			passwd root || true
			state="$("$GUARD" state)"
			[ "$state" = blank ] && {
				tries=$((tries + 1))
				if [ "$tries" -ge 5 ]; then
					echo "No password set after $tries attempts; closing this session." >&2
					# let the tty drain: the hangup on exit otherwise cuts the line short
					sleep 2
					exit 1
				fi
				echo "Password not set, try again." >&2
				sleep 1
			}
		done
		trap - INT
	fi

	"$GUARD" enforce
}

tropic01_force_password
unset -f tropic01_force_password 2>/dev/null || true
