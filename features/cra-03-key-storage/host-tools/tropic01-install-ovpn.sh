#!/bin/sh
# tropic01-install-ovpn.sh -- install an OpenVPN client profile on an OpenWrt One whose
# client key lives in TROPIC01, so the owner does not have to edit configs by hand.
#
#   tropic01-install-ovpn.sh [-i N] [-n NAME] [--no-start] <profile.ovpn> <user@router>
#
# It takes a normal .ovpn (with inline <ca> and <tls-crypt> or <tls-auth> blocks), drops the
# software identity (<cert>, <key>, cert/key lines, any pkcs11 lines), adds the TROPIC01
# PKCS#11 directives for key N, and installs the result on the router over SSH:
#
#   /etc/openvpn/NAME.conf       the derived client config   (0600)
#   /etc/openvpn/NAME-ca.crt     the CA from the profile      (0600)
#   /etc/openvpn/NAME-ta.key     the TLS key from the profile (0600, only if the profile has one)
#   /etc/config/openvpn          an enabled instance NAME, started at boot
#
# The client certificate must already be in TROPIC01 R-MEM slot N+1 for key N (see
# docs/openvpn-user-guide.md): the PKCS#11 id is discovered from the chip, and the script stops
# if there is no certificate for that key. Nothing from the profile is printed.
#
# Options:  -i N       key index = ECC slot (default 0)
#           -n NAME    instance and file name prefix (default tropic01)
#           --no-start install and enable, but do not start now
# Environment: SSH   the ssh command (default "ssh"); add options or a wrapper there.
#
# TROPIC01 allows one SPI session at a time: a running OpenVPN holds it, so the script stops
# the instance first (and starts it again at the end unless --no-start).

set -eu
umask 077

KEY_INDEX=0
NAME=tropic01
START=1
SSH="${SSH:-ssh}"

log() { echo "[tropic01-install-ovpn] $*" >&2; }
RESTART_ON_FAIL=0
die() {
	log "ERROR: $*"
	if [ "$RESTART_ON_FAIL" -eq 1 ]; then
		log "starting the OpenVPN instance that was running before"
		# shellcheck disable=SC2086
		$SSH "$ROUTER" '/etc/init.d/openvpn start' >/dev/null 2>&1 || true
	fi
	exit 1
}

while [ $# -gt 0 ]; do
	case "$1" in
	-i) KEY_INDEX="${2:?-i needs a number}"; shift 2 ;;
	-n) NAME="${2:?-n needs a name}"; shift 2 ;;
	--no-start) START=0; shift ;;
	-h | --help) sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	--) shift; break ;;
	-*) die "unknown option $1" ;;
	*) break ;;
	esac
done
[ $# -eq 2 ] || die "usage: $0 [-i N] [-n NAME] [--no-start] <profile.ovpn> <user@router>"
PROFILE="$1"
ROUTER="$2"
[ -f "$PROFILE" ] || die "no such file: $PROFILE"
case "$KEY_INDEX" in '' | *[!0-9]*) die "key index must be a number" ;; esac
case "$NAME" in '' | *[!A-Za-z0-9_-]*) die "name may only contain letters, digits, '_' and '-'" ;; esac

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM

# --- 1. take the profile apart (nothing is printed) ---------------------------------------
# Inline blocks to files; every other line is kept as a directive unless it names the software
# identity or something this script re-adds.
awk -v dir="$TMP" '
	/^<[A-Za-z0-9-]+>[[:space:]]*$/ { tag = $0; gsub(/[<>[:space:]]/, "", tag); inblock = 1; out = dir "/blk." tag; next }
	/^<\/[A-Za-z0-9-]+>[[:space:]]*$/ { inblock = 0; close(out); next }
	inblock { print > out; next }
	{ print > (dir "/directives") }
' "$PROFILE"
touch "$TMP/directives"

[ -s "$TMP/blk.ca" ] || die "the profile has no inline <ca> block"
for t in cert key; do rm -f "$TMP/blk.$t"; done

TLS_LINE=""
if [ -s "$TMP/blk.tls-crypt" ]; then
	cp "$TMP/blk.tls-crypt" "$TMP/ta.key"
	TLS_LINE="tls-crypt /etc/openvpn/$NAME-ta.key"
elif [ -s "$TMP/blk.tls-auth" ]; then
	cp "$TMP/blk.tls-auth" "$TMP/ta.key"
	DIR_LINE="$(grep -E '^[[:space:]]*key-direction[[:space:]]+[01]' "$TMP/directives" | head -1 || true)"
	TLS_LINE="tls-auth /etc/openvpn/$NAME-ta.key ${DIR_LINE##* }"
	[ -n "$DIR_LINE" ] || TLS_LINE="tls-auth /etc/openvpn/$NAME-ta.key"
else
	log "note: the profile has no tls-crypt or tls-auth block; none will be configured"
fi
for t in tls-crypt-v2 secret; do
	[ -s "$TMP/blk.$t" ] && die "the profile uses an inline <$t> block, which this script does not handle"
done

# directives: keep all but the software identity and what is re-added below
grep -vE '^[[:space:]]*($|#|;)' "$TMP/directives" |
	grep -vE '^[[:space:]]*(cert|key|ca|tls-crypt|tls-auth|key-direction|pkcs11-[a-z-]+|pkcs12|auth-user-pass|askpass)([[:space:]]|$)' \
		> "$TMP/kept" || true
grep -qE '^[[:space:]]*remote[[:space:]]' "$TMP/kept" || die "the profile has no 'remote' line"
log "profile read: $(wc -l < "$TMP/kept" | tr -d ' ') directives kept, CA ${TLS_LINE:+and TLS key }extracted (contents not shown)"

# --- 2. find the chip's PKCS#11 id for the key (the router must be free of other SPI users) -
log "stopping any running OpenVPN on the router, then reading the TROPIC01 certificates"
# shellcheck disable=SC2086  # $SSH is intentionally word-split (it may carry options)
$SSH "$ROUTER" 'if pgrep -f "openvpn --cd" >/dev/null 2>&1; then echo WAS_RUNNING; fi; /etc/init.d/openvpn stop >/dev/null 2>&1; sleep 2; openvpn --show-pkcs11-ids /usr/lib/libtropic_pkcs11.so 2>&1' > "$TMP/ids" || die "cannot run openvpn on the router (is the image's OpenVPN installed?)"
if grep -q '^WAS_RUNNING$' "$TMP/ids"; then RESTART_ON_FAIL=1; fi
SUFFIX="$(printf '/%02x' "$KEY_INDEX")"
PKID="$(awk -v suf="$SUFFIX" 'tolower($1) == "serialized" && tolower($0) ~ suf "$" { sub(/^.*Serialized id:[[:space:]]*/, ""); print; exit }' "$TMP/ids")"
if [ -z "$PKID" ]; then
	log "certificates the chip exposes:"
	grep -E 'DN:|Serialized id' "$TMP/ids" >&2 || true
	die "no certificate for key index $KEY_INDEX (R-MEM slot $((KEY_INDEX + 1))). Store the signed certificate there first: see docs/openvpn-user-guide.md, step 5"
fi
log "using the TROPIC01 certificate for key index $KEY_INDEX"

# --- 3. the derived config --------------------------------------------------------------------
{
	cat "$TMP/kept"
	echo "ca /etc/openvpn/$NAME-ca.crt"
	[ -z "$TLS_LINE" ] || echo "$TLS_LINE"
	echo "pkcs11-providers /usr/lib/libtropic_pkcs11.so"
	echo "pkcs11-id '$PKID'"
	grep -qE '^[[:space:]]*iproute[[:space:]]' "$TMP/kept" || echo "iproute /sbin/ip"
	grep -qE '^[[:space:]]*keepalive[[:space:]]' "$TMP/kept" || echo "keepalive 10 120"
} > "$TMP/conf"

# --- 4. install over SSH, files private ---------------------------------------------------------
put() { # put <local> <remote-path>
	# shellcheck disable=SC2086
	$SSH "$ROUTER" "umask 077; mkdir -p /etc/openvpn; cat > '$2'; chmod 600 '$2'" < "$1" || die "could not write $2 on the router"
}
put "$TMP/conf" "/etc/openvpn/$NAME.conf"
put "$TMP/blk.ca" "/etc/openvpn/$NAME-ca.crt"
[ -z "$TLS_LINE" ] || put "$TMP/ta.key" "/etc/openvpn/$NAME-ta.key"

# shellcheck disable=SC2086
$SSH "$ROUTER" "
uci -q delete openvpn.$NAME
uci set openvpn.$NAME=openvpn
uci set openvpn.$NAME.enabled=1
uci set openvpn.$NAME.config=/etc/openvpn/$NAME.conf
uci commit openvpn
/etc/init.d/openvpn enable
" || die "could not configure the OpenVPN instance"
log "installed instance '$NAME' (enabled at boot)"

if [ "$START" -eq 0 ]; then
	log "not started (--no-start). Start it with: /etc/init.d/openvpn start"
	exit 0
fi

# --- 5. start and wait for the tunnel -------------------------------------------------------------
# shellcheck disable=SC2086
RESTART_ON_FAIL=0
# shellcheck disable=SC2086
$SSH "$ROUTER" '/etc/init.d/openvpn start' >/dev/null 2>&1 || die "could not start OpenVPN"
log "waiting up to 90 s for the tunnel (the chip and the TLS handshake need a few seconds)"
i=0
while [ "$i" -lt 18 ]; do
	# shellcheck disable=SC2086
	ADDR="$($SSH "$ROUTER" "ip -4 -o addr show 2>/dev/null | awk '\$2 ~ /^tun/ {print \$2, \$4}' | head -1" 2>/dev/null || true)"
	[ -z "$ADDR" ] || break
	i=$((i + 1))
	sleep 5
done
if [ -n "$ADDR" ]; then
	log "tunnel is up: $ADDR"
else
	log "no tunnel yet. Look at the log on the router: logread | grep -i -E 'openvpn|pkcs11'"
	exit 1
fi
