#!/bin/bash
# tropic01-config-audit: baseline, added/changed/removed files, service enablement, changes made
# while it was not running, silence when idle, no file contents in the log, stops on SIGTERM.
set -u
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"
A="$REPO/features/cra-06-security-logging/tropic01-security-log/files/usr/libexec/tropic01-config-audit"
cd "$WORK" && mkdir -p bin etc/config rc.d

printf '#!/bin/sh\nshift 4\necho "$*" >> %s/audit.log\n' "$WORK" > bin/logger; chmod +x bin/logger
echo a > etc/config/system; echo b > etc/config/firewall; echo x > etc/shadow
touch rc.d/S19dropbear rc.d/S99foo rc.d/K10foo
export TROPIC01_AUDIT_WATCH="$WORK/etc/config/* $WORK/etc/shadow" TROPIC01_AUDIT_STATE="$WORK/state/baseline" TROPIC01_AUDIT_RCD="$WORK/rc.d"
clean() { sed "s#$WORK/##g; s/ uptime=.*//" audit.log | sort; }
run() { PATH="$WORK/bin:$PATH" timeout "$1" sh "$A" 1 >/dev/null 2>&1 || true; }

: > audit.log; run 2.5
check_contains "first start records a baseline" "config baseline recorded" "$(clean)"

: > audit.log
( sleep 2; echo changed > etc/config/system; echo new > etc/config/dhcp; rm etc/config/firewall; touch rc.d/S50bar; rm rc.d/S99foo ) &
run 6; wait
out="$(clean)"
check_contains "an added file is reported"        "config added etc/config/dhcp" "$out"
check_contains "a changed file is reported"       "config changed etc/config/system" "$out"
check_contains "a removed file is reported"       "config removed etc/config/firewall" "$out"
check_contains "an enabled service is reported"   "service enabled bar" "$out"
check_contains "a disabled service is reported"   "service disabled foo" "$out"

: > audit.log; echo offline > etc/shadow; rm -f etc/config/dhcp; run 2.5
out="$(clean)"
check_contains "a change made while not running is caught" "(while not running) config changed etc/shadow" "$out"
check_contains "a removal made while not running is caught" "(while not running) config removed etc/config/dhcp" "$out"
check_absent   "file contents are never logged" "offline" "$(cat audit.log)"

: > audit.log; run 3.5
check "nothing is logged when nothing changed" "0" "$(wc -l < audit.log | tr -d ' ')"
check "the stored baseline is private (0600)" "600" "$(stat -c %a state/baseline)"

: > audit.log
PATH="$WORK/bin:$PATH" sh "$A" 1 >/dev/null 2>&1 & P=$!; track "$P"; sleep 1.5; kill -TERM "$P"
for _ in 1 2 3 4 5 6; do kill -0 "$P" 2>/dev/null || break; sleep 0.5; done
kill -0 "$P" 2>/dev/null && state=running || state=exited
check "SIGTERM stops the loop (procd can stop the service)" "exited" "$state"
finish
