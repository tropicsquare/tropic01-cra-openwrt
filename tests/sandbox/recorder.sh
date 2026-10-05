#!/bin/bash
# tropic01-log-recorder, run under busybox sh (the router's shell family) with a fake `logread` that,
# like the real one, replays only the LAST buffered line when following. Checks: history and live events,
# no duplicates on restart, a rebooted buffer, rotation by size, clean shutdown, the start-up race, and
# a follower that replays nothing (the case that once made the recorder swallow every event).
set -u
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"
command -v busybox >/dev/null 2>&1 || skip "busybox not installed (apt install busybox): the recorder needs 'read -t'"
R="$REPO/features/cra-06-security-logging/tropic01-security-log/files/usr/libexec/tropic01-log-recorder"
cd "$WORK" && mkdir -p bin

cat > bin/logread <<'EOF2'
#!/bin/sh
follow=0
for a in "$@"; do [ "$a" = "-f" ] && follow=1; done
if [ "$follow" = 1 ]; then
	[ -n "${FAKE_NOREPLAY:-}" ] || tail -n 1 "$FAKE_BUF" 2>/dev/null
	n=$(wc -l < "$FAKE_FEED")
	while sleep 0.2; do
		m=$(wc -l < "$FAKE_FEED")
		if [ "$m" -gt "$n" ]; then sed -n "$((n + 1)),${m}p" "$FAKE_FEED"; n=$m; fi
	done
fi
cat "$FAKE_BUF" 2>/dev/null
EOF2
chmod +x bin/logread
export TROPIC01_LOGREAD="$WORK/bin/logread" FAKE_BUF="$WORK/buf" FAKE_FEED="$WORK/feed" TROPIC01_LOG="$WORK/out/security.log"
: > feed

start() { busybox sh "$R" "$1" 'x' >/dev/null 2>&1 & REC=$!; track "$REC"; sleep 1.5; }
stop()  { kill -TERM "$REC" 2>/dev/null; for _ in 1 2 3 4 5 6; do kill -0 "$REC" 2>/dev/null || break; sleep 0.4; done; }
content() { tr '\n' ' ' < "$TROPIC01_LOG" | sed 's/ $//'; }

printf 'L1\nL2\nL3\n' > buf; start 100000; echo L4 >> feed; sleep 0.8
check "buffered history is recorded and a live event is written at once" "L1 L2 L3 L4" "$(content)"; stop

printf 'L1\nL2\nL3\nL4\n' > buf; echo L5 >> buf; start 100000; echo L6 >> feed; sleep 0.8
check "a restart does not duplicate stored history" "L1 L2 L3 L4 L5 L6" "$(content)"; stop

printf 'N1\nN2\n' > buf; start 100000; sleep 0.5
check "after a reboot (new buffer) everything new is kept" "L1 L2 L3 L4 L5 L6 N1 N2" "$(content)"; stop

rm -rf out; : > buf; start 60
for i in 1 2 3 4 5 6 7 8; do echo "event-number-$i-padding" >> feed; sleep 0.25; done; sleep 0.5
[ -f out/security.log.1 ] && rot=yes || rot=no
check "rotation by size produces a rotated file" "yes" "$rot"
cur=$(wc -c < out/security.log | tr -d ' ')
[ "$cur" -le 90 ] && small=yes || small=no
check "the current file stays near the cap" "yes" "$small"; stop

: > buf; start 100000; kill -TERM "$REC"; sleep 1.5
kill -0 "$REC" 2>/dev/null && st=running || st=exited
check "SIGTERM stops the recorder" "exited" "$st"
check "its FIFO is removed" "0" "$(find /tmp -maxdepth 1 -name 'tropic01-recorder.*' 2>/dev/null | wc -l | tr -d ' ')"

rm -rf out; printf 'H1\nH2\n' > buf; : > feed
busybox sh "$R" 100000 'x' >/dev/null 2>&1 & REC=$!; track "$REC"
echo H3 >> buf; echo H3 >> feed; sleep 1.5; echo H4 >> buf; echo H4 >> feed; sleep 0.8
check "an event logged right at start-up is neither lost nor doubled" "H1 H2 H3 H4" "$(content)"; stop

rm -rf out; printf 'G1\nG2\n' > buf; : > feed
FAKE_NOREPLAY=1 busybox sh "$R" 100000 'x' >/dev/null 2>&1 & REC=$!; track "$REC"
sleep 1.5; echo G3 >> buf; echo G3 >> feed; sleep 0.8; echo G4 >> buf; echo G4 >> feed; sleep 0.8
check "a follower that replays nothing must not stop later events" "G1 G2 G3 G4" "$(content)"; stop
finish
