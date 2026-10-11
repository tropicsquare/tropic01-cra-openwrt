#!/bin/bash
# tropic01-update (cra-07): acceptance of signed sysupgrade images, the root key in TROPIC01 and the rollback floor, run
# with OpenWrt's own usign, ucert and fwtool (from a build tree: $OPENWRT_DIR/staging_dir/host/bin, or the PATH) and the real
# tropicctl built with its fake chip. It checks the logic and the gates, not a router: see tests/hardware for that.
set -u
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"
command -v cmake >/dev/null 2>&1 || skip "cmake not available"
HOSTBIN="${OPENWRT_DIR:-$REPO/build/openwrt}/staging_dir/host/bin"
[ -d "$HOSTBIN" ] && PATH="$HOSTBIN:$PATH"
for t in usign ucert fwtool; do command -v "$t" >/dev/null 2>&1 || skip "$t not available (build OpenWrt's host tools, or set OPENWRT_DIR)"; done
U="${TROPIC01_UPDATE_SCRIPT:-$REPO/features/cra-07-secure-update/tropic01-secure-update/files/usr/sbin/tropic01-update}"
HOOK="$REPO/features/cra-07-secure-update/tropic01-secure-update/files/lib/upgrade/zz-tropic01-secure-update.sh"
cd "$WORK" || exit 1
if ! { cmake -S "$REPO/features/core/tropicctl" -B build -DTROPICCTL_CHIP=fake && cmake --build build; } > cmake.log 2>&1; then
	cat cmake.log; echo "FAIL  build tropicctl (fake chip)"; exit 1
fi
BIN="$WORK/build/tropicctl"
mkdir -p bin keys
# fake pidof / OpenVPN init: "running" while the file ovpn.running exists; every stop and start is logged
cat > bin/pidof <<'EOF2'
#!/bin/sh
[ "$1" = openvpn ] && [ -e "$WORK/ovpn.running" ] && { echo 4242; exit 0; }
exit 1
EOF2
cat > bin/openvpn-init <<'EOF2'
#!/bin/sh
echo "$1" >> "$WORK/ovpn.log"
case "$1" in stop) rm -f "$WORK/ovpn.running" ;; start) touch "$WORK/ovpn.running" ;; esac
EOF2
chmod +x bin/pidof bin/openvpn-init
export WORK PATH="$WORK/bin:$PATH"
export TROPICCTL_FAKE_DIR="$WORK/chip" TROPICCTL_FAKE_INIT=1 TROPICCTL_LOCK="$WORK/lock" TROPICCTL_NO_DEPRECATION=1 TROPICCTL_KEY_DIR="$WORK/etc"
export TROPIC01_UPDATE_TROPICCTL="$BIN" TROPIC01_UPDATE_SLOTS="200 201" TROPIC01_UPDATE_COUNTER=0 TROPIC01_UPDATE_START=1000
export TROPIC01_UPDATE_ENFORCE=1 TROPIC01_UPDATE_IMAGE_SV="$WORK/image-sv" TROPIC01_UPDATE_OPENVPN_INIT="$WORK/bin/openvpn-init"
T() { "$BIN" "$@"; }
run() { "$U" "$@" > out.txt 2> err.txt; echo $?; }
newchip() { rm -rf chip; T info fw >/dev/null 2>&1; }

# keys: a root, a release key with a certificate from that root, and an unrelated root with its own release key
usign -G -p keys/root.pub -s keys/root.sec -c "update root" && usign -G -p keys/rel.pub -s keys/rel.sec -c "release"
ucert -I -c keys/rel.ucert -p keys/rel.pub -s keys/root.sec
usign -G -p keys/root2.pub -s keys/root2.sec -c "other root" && usign -G -p keys/rel2.pub -s keys/rel2.sec -c "other release"
ucert -I -c keys/rel2.ucert -p keys/rel2.pub -s keys/root2.sec
# mkimg NAME SV [release-key-prefix]: an image as OpenWrt's append-metadata makes it (metadata, usign signature, ucert chain)
mkimg() {
	head -c 20000 /dev/urandom > "$1"
	if [ "$2" = none ]; then meta='{"metadata_version":"1.1","supported_devices":["openwrt,one"]}'
	else meta="{\"tropic01_sv\": $2, \"metadata_version\":\"1.1\",\"supported_devices\":[\"openwrt,one\"]}"; fi
	echo "$meta" | fwtool -I - "$1"
	[ "${3:-rel}" = unsigned ] && return 0
	cp "keys/${3:-rel}.ucert" "$1.ucert"
	usign -S -m "$1" -s "keys/${3:-rel}.sec" -x "$1.sig"
	ucert -A -c "$1.ucert" -x "$1.sig"
	fwtool -S "$1.ucert" "$1"
}
mkimg img3 3; mkimg img5 5; mkimg img7 7; mkimg img1 1; mkimg nosv none; mkimg unsigned 4 unsigned; mkimg foreign 9 rel2
cp img7 tampered; printf 'X' | dd of=tampered bs=1 seek=500 conv=notrunc 2>/dev/null

echo "# usage"
newchip
check "no arguments: usage, exit 2" 2 "$(run)"
check "a bad counter setting is refused" 1 "$(TROPIC01_UPDATE_COUNTER=99 run status)"

echo "# fail closed before provisioning"
check "check with no root key in the chip: refused" 1 "$(run check img3)"
check_contains "and says so" "no update root key in TROPIC01" "$(cat err.txt)"

echo "# keys"
check "install without its flag: dry run" 0 "$(run keys install 200 keys/root.pub)"
check_contains "and says so" "DRY RUN" "$(cat err.txt)"
check "nothing was written" "no" "$([ -e chip/rmem/200 ] && echo yes || echo no)"
check "a slot that is not an update key slot is refused" 1 "$(run keys install 50 keys/root.pub --yes-write-update-key)"
head -c 86 /dev/urandom > junk.pub
check "a file that is not a usign public key is refused" 1 "$(run keys install 200 junk.pub --yes-write-update-key)"
check "install" 0 "$(run keys install 200 keys/root.pub --yes-write-update-key)"
check "the chip holds the key" "yes" "$(cmp -s chip/rmem/200 keys/root.pub && echo yes || echo no)"
check "an occupied slot needs --replace" 1 "$(run keys install 200 keys/root2.pub --yes-write-update-key)"
check "status lists the key by fingerprint" 0 "$(run status)"
check_contains "with its fingerprint" "slot 200: key $(usign -F -p keys/root.pub)" "$(cat err.txt)"

echo "# the rollback counter"
check "check before the counter is initialised: refused" 1 "$(run check img3)"
check_contains "and says so" "not initialised" "$(cat err.txt)"
check "floor init without its flag: dry run" 0 "$(run floor init)"
check "the counter is still uninitialised" 6 "$(T counter get 0 >/dev/null 2>&1; echo $?)"
check "floor init" 0 "$(run floor init --yes-init-rollback-counter)"
check "the counter starts at 1000" "counter 0: 1000" "$(T counter get 0 2>/dev/null)"
check "a second init is refused (it would lower the floor)" 1 "$(run floor init --yes-init-rollback-counter)"

echo "# acceptance"
check "a signed image with security version 3: accepted" 0 "$(run check img3)"
check_contains "and the reason is logged" "accepted: signed by a root held in TROPIC01, security version 3 (floor 0)" "$(cat err.txt)"
check "an unsigned image: refused" 1 "$(run check unsigned)"
check_contains "no signature" "carries no signature" "$(cat err.txt)"
check "a signed image without a security version: refused" 1 "$(run check nosv)"
check_contains "says which" "no security version" "$(cat err.txt)"
check "a tampered image: refused" 1 "$(run check tampered)"
check "an image signed under another root: refused" 1 "$(run check foreign)"
check_contains "says why" "does not lead to an update root key held in TROPIC01" "$(cat err.txt)"
check "no file: refused" 1 "$(run check nothere)"

echo "# rollback floor"
echo 5 > image-sv
check "commit after booting security version 5" 0 "$(run commit)"
check_contains "raises the floor" "floor raised from 0 to 5" "$(cat err.txt)"
check "the counter went down by 5" "counter 0: 995" "$(T counter get 0 2>/dev/null)"
check "security version 3 is now a rollback: refused" 1 "$(run check img3)"
check_contains "says so" "below this unit's floor 5" "$(cat err.txt)"
check "security version 5 is accepted (same level)" 0 "$(run check img5)"
check "security version 7 is accepted" 0 "$(run check img7)"
check "commit again: nothing to raise" 0 "$(run commit)"
check "the counter is unchanged" "counter 0: 995" "$(T counter get 0 2>/dev/null)"
echo 2 > image-sv
check "booting an older image never lowers the floor" 0 "$(run commit)"
check "the counter is unchanged" "counter 0: 995" "$(T counter get 0 2>/dev/null)"
echo 1001 > image-sv
check "a security version above the counter's range is an error" 1 "$(run commit)"
rm -f image-sv
check "no security version in the running image: nothing to commit" 0 "$(run commit)"

echo "# a second root (planned root change)"
check "install the other root in slot 201" 0 "$(run keys install 201 keys/root2.pub --yes-write-update-key)"
check "an image under the second root is accepted" 0 "$(run check foreign)"
T mem erase 201 >/dev/null 2>&1

echo "# enforcement off (development only)"
check "with enforce=0 an unsigned image passes" 0 "$(TROPIC01_UPDATE_ENFORCE=0 run check unsigned)"
check_contains "with a warning" "NOT checked" "$(cat err.txt)"

echo "# chip trouble fails closed"
touch chip/down
check "chip unreachable: refused" 1 "$(TROPIC01_UPDATE_SLOTS=200 run check img7)"
check_contains "says why" "cannot read update key slot 200" "$(cat err.txt)"
rm -f chip/down

echo "# OpenVPN is stopped while the chip is used, and started again"
touch ovpn.running; : > ovpn.log
check "check with OpenVPN running" 0 "$(run check img7)"
check "OpenVPN was stopped and started again" "stop start" "$(tr '\n' ' ' < ovpn.log | sed 's/ $//')"
check "and is running again" "yes" "$([ -e ovpn.running ] && echo yes || echo no)"
: > ovpn.log
check "a refused image also starts OpenVPN again" 1 "$(run check foreign)"
check "stop and start" "stop start" "$(tr '\n' ' ' < ovpn.log | sed 's/ $//')"
rm -f ovpn.running

echo "# the sysupgrade hook: a refusal cannot be forced"
sed "s#/usr/sbin/tropic01-update#$U#" "$HOOK" > hook.sh
# shellcheck disable=SC1091,SC2329  # hook.sh is generated above; notify_firmware_broken is called by the hook
hook() { # hook IMAGE -> "rc FORCEABLE"
	( FORCEABLE=1; notify_firmware_broken() { FORCEABLE=0; }
	  . ./hook.sh; rc=0; fwtool_check_signature "$1" 2>/dev/null || rc=$?; echo "$rc $FORCEABLE" )
}
check "an accepted image: 0, still forceable (other checks decide)" "0 1" "$(hook img7)"
check "a refused image: 1 and NOT forceable (sysupgrade -F cannot install it)" "1 0" "$(hook unsigned)"
check "a rollback: 1 and not forceable" "1 0" "$(hook img1)"
finish
