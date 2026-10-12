#!/bin/bash
# tropic01-pairing: the provisioning flow and its safety rules, run against the real tropicctl built with its
# fake chip (state in files). It checks the logic and the gates, not the chip: see tests/hardware for that.
set -u
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"
command -v cmake >/dev/null 2>&1 || skip "cmake not available"
P="${TROPIC01_PAIRING_SCRIPT:-$REPO/features/cra-04-access-control/tropic01-pairing/files/usr/sbin/tropic01-pairing}"
cd "$WORK" || exit 1
if ! { cmake -S "$REPO/features/core/tropicctl" -B build -DTROPICCTL_CHIP=fake && cmake --build build; } > cmake.log 2>&1; then
	cat cmake.log; echo "FAIL  build tropicctl (fake chip)"; exit 1
fi
BIN="$WORK/build/tropicctl"
export TROPICCTL_FAKE_DIR="$WORK/chip" TROPICCTL_FAKE_INIT=1 TROPICCTL_LOCK="$WORK/lock" TROPICCTL_NO_DEPRECATION=1
export TROPIC01_DIR="$WORK/etc" TROPIC01_PAIRING_TROPICCTL="$BIN" TROPICCTL_KEY_DIR="$WORK/etc"
T() { "$BIN" "$@"; }
run() { "$P" "$@" > out.txt 2> err.txt; echo $?; }
cfg() { grep "^$1 " chip/rconfig | awk '{print $2}'; }
# newunit [fw]: a fresh fake chip (firmware 2.2.0 unless given) and a fresh per-unit key in slot 1
newunit() {
	rm -rf chip etc keys; mkdir -p etc
	T info fw >/dev/null 2>&1
	echo "${1:-00000202 00000301}" > chip/fw
	head -c 32 /dev/urandom > etc/pairing.key; head -c 32 /dev/urandom > etc/pairing.pub; echo 1 > etc/pairing.slot
}

echo "# usage and key files"
newunit
check "no arguments: usage, exit 2" 2 "$(run)"
check "unknown option: exit 1" 1 "$(run provision --bogus)"
rm etc/pairing.key
check "missing key files: exit 1" 1 "$(run provision)"
check_contains "and says what to do" "tropic01-gen-pairing-key.sh" "$(cat err.txt)"
newunit; head -c 5 /dev/urandom > etc/pairing.pub
check "a public key of the wrong size: exit 1" 1 "$(run provision)"
newunit; echo 0 > etc/pairing.slot
check "pairing.slot 0 is refused" 1 "$(run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
check_contains "with the reason" "never written" "$(cat err.txt)"
check "slot 0 was not touched" "yes" "$([ "$(wc -c < chip/pairing/0 | tr -d ' ')" = 32 ] && [ ! -e chip/pairing/1 ] && echo yes || echo no)"
echo 4 > etc/pairing.slot
check "pairing.slot 4 is refused" 1 "$(run provision)"

echo "# firmware gate"
newunit "00020100 00010300"
check "firmware older than 2.0.0 is refused" 1 "$(run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
check_contains "with the reason" "older than 2.0.0" "$(cat err.txt)"
check "nothing was written" "no" "$([ -e chip/pairing/1 ] && echo yes || echo no)"
check "--allow-old-firmware lets it through (dry run here)" 0 "$(run provision --allow-old-firmware)"
check_contains "and it is still a dry run" "DRY RUN" "$(cat err.txt)"

echo "# dry runs write nothing"
newunit
check "provision without flags: dry run, exit 0" 0 "$(run provision)"
check_contains "says it would write the slot" "needs --yes-write-pairing-slot" "$(cat err.txt)"
check "pairing slot 1 is still empty" "no" "$([ -e chip/pairing/1 ] && echo yes || echo no)"
check "status works before provisioning" 0 "$(run status)"
check_contains "and says the shared key has full rights" "NOT restricted" "$(cat err.txt)"

echo "# the full flow"
newunit
check "write the slot only (no config flag)" 0 "$(run provision --yes-write-pairing-slot)"
check_contains "the slot was written" "written and read back" "$(cat err.txt)"
check_contains "and the rewrite is a dry run" "needs --yes-rewrite-r-config" "$(cat err.txt)"
check "the slot holds the unit's key" "yes" "$(cmp -s chip/pairing/1 etc/pairing.pub && echo yes || echo no)"
check "the shared key still has its rights" 6 "$(T --key shared mem read 1 >/dev/null 2>&1; echo $?)"
check "the R-config was not touched" "0xffffffff" "$(cfg TR01_CFG_UAP_R_MEM_DATA_READ)"
check "provision with both flags (the slot is already written)" 0 "$(run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
check_contains "verified" "verified: the shared key is refused" "$(cat err.txt)"
check_contains "and says restricted, not invalidated" "restricted, not invalidated" "$(cat err.txt)"
check "the shared key is refused for the configuration: exit 5" 5 "$(T --key shared config dump >/dev/null 2>&1; echo $?)"
check "the shared key is refused for R-MEM: exit 5" 5 "$(T --key shared mem read 1 >/dev/null 2>&1; echo $?)"
check "the shared key is refused for ECC: exit 5" 5 "$(T --key shared ecc generate 5 --curve p256 >/dev/null 2>&1; echo $?)"
check "the shared key still opens a session (ping stays)" 0 "$(T --key shared session test >/dev/null 2>&1; echo $?)"
check "the per-unit key still works" 6 "$(T --key unit mem read 1 >/dev/null 2>&1; echo $?)"
check "the first configuration was saved" 27 "$(grep -c '^R ' etc/rconfig-before.cfg)"
check "and it is the original (shared key had all rights)" "0xffffffff" "$(grep '^R TR01_CFG_UAP_R_MEM_DATA_READ' etc/rconfig-before.cfg | awk '{print $3}')"
check "slot 0 was NOT invalidated" "no" "$([ -e chip/pairing/0.invalid ] && echo yes || echo no)"
check "status says restricted" 0 "$(run status)"
check_contains "in words" "refused for the configuration (restricted)" "$(cat err.txt)"
check_absent "and shows no raw chip error (the slots are listed with the per-unit key)" "UNAUTHORIZED" "$(cat err.txt)"
check_contains "and lists the unit's slot" "pairing slot 1: populated" "$(cat err.txt)"
sum1="$(cksum < etc/rconfig-before.cfg)"
check "provision again: already done, exit 0" 0 "$(run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
check_contains "and says so" "already restricted" "$(cat err.txt)"
check "the saved first configuration is unchanged" "$sum1" "$(cksum < etc/rconfig-before.cfg)"

echo "# undo"
check "undo without its flag is a dry run" 0 "$(run undo)"
check_contains "and says what it would do" "needs --yes-rewrite-r-config" "$(cat err.txt)"
check "the shared key is still refused" 5 "$(T --key shared config dump >/dev/null 2>&1; echo $?)"
check "undo" 0 "$(run undo --yes-rewrite-r-config)"
check "the shared key has its rights again after the restart" 0 "$(T --key shared config dump >/dev/null 2>&1; echo $?)"
check "undo with no saved configuration: exit 1" 1 "$(rm -f etc/rconfig-before.cfg; run undo --yes-rewrite-r-config)"

echo "# refusals before any write"
newunit; head -c 32 /dev/urandom > chip/pairing/1
check "a slot that holds another key: refused" 1 "$(run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
check_contains "with the reason" "different key" "$(cat err.txt)"
check "the shared key keeps its rights" "0xffffffff" "$(cfg TR01_CFG_UAP_R_MEM_DATA_READ)"
newunit; head -c 32 /dev/urandom > chip/pairing/1; touch chip/pairing/1.invalid
check "an invalidated slot: refused" 1 "$(run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
check_contains "with the reason" "invalidated" "$(cat err.txt)"
newunit
check "a write that does not stick (old-firmware behaviour): exit 1" 1 "$(TROPICCTL_FAKE_PAIRING_SILENT=1 run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
check "and nothing was restricted" "0xffffffff" "$(cfg TR01_CFG_UAP_R_MEM_DATA_READ)"
newunit; touch chip/down
check "the chip is down: exit 1" 1 "$(run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
rm -f chip/down

echo "# verification failure restores the configuration"
cat > "$WORK/noreboot" <<'EOF2'
#!/bin/sh
# tropicctl that does not restart the chip: the new R-config never comes into force, so verification must fail
[ "$1 $2" = "chip reboot" ] && exit 0
exec "$TROPIC01_REAL_TROPICCTL" "$@"
EOF2
chmod +x "$WORK/noreboot"
newunit
check "restriction that does not take effect: exit 1" 1 "$(TROPIC01_REAL_TROPICCTL="$BIN" TROPIC01_PAIRING_TROPICCTL="$WORK/noreboot" run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
check_contains "the failure is reported" "verification FAILED" "$(cat err.txt)"
check_contains "and the configuration restored" "restored the previous configuration" "$(cat err.txt)"
check "the chip's R-config is the original again" "0xffffffff" "$(cfg TR01_CFG_UAP_R_MEM_DATA_READ)"

echo "# nothing is restricted unless the per-unit key opens a session"
cat > "$WORK/unitfail" <<'EOF2'
#!/bin/sh
# tropicctl whose per-unit key never opens a session (a damaged private key, a wrong slot)
case "$*" in *"--key unit"*"session test"*) echo "tropicctl: error: secure session failed: LT_L2_HSK_ERR" >&2; exit 4 ;; esac
exec "$TROPIC01_REAL_TROPICCTL" "$@"
EOF2
chmod +x "$WORK/unitfail"
newunit
check "the per-unit key does not open a session: exit 1" 1 "$(TROPIC01_REAL_TROPICCTL="$BIN" TROPIC01_PAIRING_TROPICCTL="$WORK/unitfail" run provision --yes-write-pairing-slot --yes-rewrite-r-config)"
check_contains "and says nothing is restricted" "nothing is restricted" "$(cat err.txt)"
check "the shared key keeps its rights" "0xffffffff" "$(cfg TR01_CFG_UAP_R_MEM_DATA_READ)"

echo "# the shared key is never used to write the configuration"
newunit
T --key shared pairing write 1 --in etc/pairing.pub --yes-write-pairing-slot >/dev/null 2>&1
run provision --yes-write-pairing-slot --yes-rewrite-r-config >/dev/null
check "the rewrite was made with the per-unit key: the shared key lost the right to rewrite" 5 "$(T --key shared config rewrite --in etc/rconfig-before.cfg --yes-rewrite-r-config >/dev/null 2>&1; echo $?)"
finish
