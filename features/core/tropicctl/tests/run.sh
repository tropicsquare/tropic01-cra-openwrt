#!/bin/bash
# Tests for tropicctl on a host without a chip, using the hand-written fake chip (src/chip_fake.c).
# Hardware tests are separate: see tests/hardware.md. Usage: tests/run.sh [path-to-fake-binary]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib.sh
. "$HERE/lib.sh"
command -v cmake >/dev/null 2>&1 || { echo "SKIP  cmake not available"; exit 0; }
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
BIN="${1:-}"
if [ -z "$BIN" ]; then
	if ! { cmake -S "$HERE/.." -B "$WORK/build" -DTROPICCTL_CHIP=fake && cmake --build "$WORK/build"; } >"$WORK/cmake.log" 2>&1; then
		cat "$WORK/cmake.log"; echo "FAIL  build"; exit 1
	fi
	BIN="$WORK/build/tropicctl"
fi
cd "$WORK" || exit 1
export TROPICCTL_FAKE_DIR="$WORK/chip" TROPICCTL_FAKE_INIT=1 TROPICCTL_LOCK="$WORK/lock" TROPICCTL_KEY_DIR="$WORK/keys" TROPICCTL_NO_DEPRECATION=1
mkdir -p keys
T() { "$BIN" "$@"; }
rc() { "$BIN" "$@" >out.txt 2>err.txt; echo $?; }
L() { TROPICCTL_ARGV0=lt-util "$BIN" "$@"; }
lrc() { TROPICCTL_ARGV0=lt-util "$BIN" "$@" >out.txt 2>err.txt; echo $?; }
fresh() { rm -rf chip keys && mkdir keys; T info fw >/dev/null 2>&1; } # a new fake chip with the defaults
json_ok() { python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$1" >/dev/null 2>&1 && echo valid || echo invalid; }
HAVE_PY=0; command -v python3 >/dev/null 2>&1 && HAVE_PY=1
setkey() { # setkey <slot>: per-unit key files and the matching public key in that chip slot
	head -c 32 /dev/urandom > keys/pairing.key; head -c 32 /dev/urandom > keys/pairing.pub; echo "$1" > keys/pairing.slot
	cp keys/pairing.pub "chip/pairing/$1"
}

# ------------------------------------------------------------------ usage, help, docs
fresh
check "--version" "tropicctl 0.1.0" "$(T --version)"
check "help lists the areas" 0 "$(T help | grep -c -E '^  (info|rng|mem|pairing|config|session)$' | awk '{print ($1==6)?0:1}')"
check "help for one command shows its class" 0 "$(T help mem write | grep -c 'Safety class: W' | awk '{print ($1==1)?0:1}')"
check "no arguments prints the overview" 0 "$(rc; )"
check "unknown command is a usage error" 2 "$(rc bogus cmd)"
check "an area without a verb is a usage error" 2 "$(rc mem)"
check "an unknown option is refused, not ignored" 2 "$(rc mem read 5 --bogus)"
check "an option without its value is refused" 2 "$(rc mem read 5 --out)"
check "an unknown global option is refused" 2 "$(rc --bogus info chip)"
check "extra positional arguments are refused" 2 "$(rc mem read 5 6)"
T help --format markdown > gen.md; T help --format man > gen.1
check "committed reference.md matches the command table" "" "$(diff -q "$HERE/../docs/reference.md" gen.md 2>&1)"
check "committed manual page matches the command table" "" "$(diff -q "$HERE/../docs/tropicctl.1" gen.1 2>&1)"
check "every command in the table appears in the reference" 0 "$(for c in 'info chip' 'info fw' 'rng get' 'mem list' 'mem read' 'mem write' 'mem erase' 'pairing list' 'pairing read' 'config dump' 'session test'; do grep -q "#### \`$c" gen.md || echo missing; done | wc -l | tr -d ' ')"

# ------------------------------------------------------------------ info
fresh
check "info chip" 0 "$(rc info chip)"
check_contains "info chip prints the CHIP_ID block" "CHIP_ID ver" "$(cat out.txt)"
check "info chip needs no session (no key warning)" "" "$(cat err.txt)"
check "info fw" 0 "$(rc info fw)"
check "info fw output (raw bytes, then the version they stand for)" "RISCV_FW_VER 00 02 01 00
SPECT_FW_VER 00 01 03 00
RISC-V firmware 0.1.2, SPECT firmware 0.3.1" "$(cat out.txt)"
if [ "$HAVE_PY" = 1 ]; then
	rc --json info chip >/dev/null; check "info chip --json is valid JSON" valid "$(json_ok out.txt)"
	rc --json info fw >/dev/null; check "info fw --json is valid JSON" valid "$(json_ok out.txt)"
	check_contains "info fw --json" '"riscv_fw":"00020100"' "$(cat out.txt)"
	check_contains "info fw --json decodes the version" '"riscv_version":"0.1.2"' "$(cat out.txt)"
fi

# ------------------------------------------------------------------ sessions and keys
fresh
check "session test with no per-unit files uses the shared key" 0 "$(rc session test)"
check_contains "and warns on stderr" "no per-unit pairing key" "$(cat err.txt)"
check_contains "and says which key opened it" "shared engineering-sample key, slot 0" "$(cat out.txt)"
check "--key unit without files refused" 4 "$(rc --key unit session test)"
check "--key shared" 0 "$(rc --key shared session test)"
check "--key pin without the PIN-protected key files is refused" 4 "$(rc --key pin session test)"
check "unknown key source" 2 "$(rc --key bogus session test)"
head -c 32 /dev/urandom > keys/pairing.key; echo 1 > keys/pairing.slot; head -c 5 /dev/urandom > keys/pairing.pub
check "unusable per-unit files: refused" 4 "$(rc session test)"
check_contains "no silent fallback to the shared key" "not falling back to the shared key" "$(cat err.txt)"
setkey 1; rm -f chip/pairing/1
check "per-unit key the chip does not know: session refused" 4 "$(rc session test)"
setkey 1
check "per-unit key the chip knows: session opens" 0 "$(rc session test)"
check_contains "and the notice names the slot" "using the per-unit pairing key, slot 1" "$(cat err.txt)"
echo 9 > keys/pairing.slot
check "pairing.slot out of range: refused" 4 "$(rc session test)"
setkey 1; PRIV="$(od -An -tx1 -v keys/pairing.key | tr -d ' \n')"
{ T session test; T pairing list; T mem list --range 0-3; T config dump; T --json pairing list; } >all.txt 2>&1
check_absent "the private key never appears in any output" "$PRIV" "$(od -An -tx1 -v all.txt | tr -d ' \n')"
fresh; setkey 1
cp keys/pairing.key keys/other.key; cp keys/pairing.pub keys/other.key.pub
check "--key file:PATH:SLOT (the public key is PATH.pub)" 0 "$(rc --key "file:$WORK/keys/other.key:1" session test)"
check "--key file: with a bad slot" 2 "$(rc --key "file:$WORK/keys/other.key:9" session test)"
check "--key file: with a missing file" 4 "$(rc --key "file:$WORK/keys/none.key:1" session test)"

# ------------------------------------------------------------------ pairing
fresh
check "pairing list" 0 "$(rc pairing list)"
check "slot states" "pairing slot 0: populated
pairing slot 1: empty
pairing slot 2: empty
pairing slot 3: empty" "$(cat out.txt)"
touch chip/pairing/2.invalid
check_contains "an invalidated slot is reported" "pairing slot 2: invalidated" "$(T pairing list 2>/dev/null)"
check "pairing read slot 0 to a file" 0 "$(rc pairing read 0 --out p0.bin)"
check "the file holds 32 bytes" 32 "$(wc -c < p0.bin | tr -d ' ')"
check "pairing read of an empty slot: not found" 6 "$(rc pairing read 1)"
check "pairing read of an invalidated slot: not found" 6 "$(rc pairing read 2)"
check "pairing read slot 7" 2 "$(rc pairing read 7)"
check "pairing read prints hex without --out" 64 "$(T pairing read 0 2>/dev/null | tr -d '\n' | wc -c | tr -d ' ')"
if [ "$HAVE_PY" = 1 ]; then rc --json pairing list >/dev/null; check "pairing list --json is valid" valid "$(json_ok out.txt)"; fi

# ------------------------------------------------------------------ R-MEM
fresh
printf 'hello vault' > in.bin
check "mem write" 0 "$(rc mem write 5 --in in.bin)"
check_contains "mem write reads back" "read them back" "$(cat out.txt)"
check "mem read to a file" 0 "$(rc mem read 5 --out got.bin)"
check "content round-trips" yes "$(cmp -s in.bin got.bin && echo yes || echo no)"
check "mem read prints hex" "68656c6c6f207661756c74" "$(T mem read 5 2>/dev/null)"
check "writing an occupied slot is refused" 7 "$(rc mem write 5 --in in.bin)"
check_contains "and says how to override" "--replace" "$(cat err.txt)"
printf 'second' > in2.bin
check "--replace erases and rewrites" 0 "$(rc mem write 5 --in in2.bin --replace)"
T mem read 5 --out got2.bin 2>/dev/null; check "the replaced content" yes "$(cmp -s in2.bin got2.bin && echo yes || echo no)"
check "--dry-run writes nothing" 0 "$(rc --dry-run mem write 6 --in in.bin)"
check_contains "and says so" "DRY RUN (nothing written)" "$(cat err.txt)"
check "slot 6 is still empty after the dry run" 6 "$(rc mem read 6)"
check "mem list" 0 "$(rc mem list --range 0-10)"
check "mem list shows the populated slot" "slot 5: 6 bytes" "$(cat out.txt)"
check "mem erase" 0 "$(rc mem erase 5)"
check "the erased slot reads empty" 6 "$(rc mem read 5)"
check "erasing an empty slot is not an error" 0 "$(rc mem erase 5)"
check "slot 512 refused" 2 "$(rc mem read 512)"
check "slot -1 refused" 2 "$(rc mem read -- -1)"
check "non-numeric slot refused" 2 "$(rc mem read abc)"
: > empty.bin
check "an empty input file is refused" 2 "$(rc mem write 7 --in empty.bin)"
head -c 476 /dev/zero > big.bin
check "a file of 476 bytes is refused" 2 "$(rc mem write 7 --in big.bin)"
head -c 475 /dev/zero | tr '\0' 'x' > max.bin
check "a file of 475 bytes is accepted" 0 "$(rc mem write 7 --in max.bin)"
check "write without --in refused" 2 "$(rc mem write 8)"
check "stdin input" 0 "$(printf 'via stdin' | T mem write 9 --in - >out.txt 2>err.txt; echo $?)"
check "stdin content stored" "7669612073746469 6e" "$(T mem read 9 2>/dev/null | sed 's/\(.\{16\}\)/\1 /')"
if [ "$HAVE_PY" = 1 ]; then rc --json mem list --range 0-10 >/dev/null; check "mem list --json is valid" valid "$(json_ok out.txt)"; fi

# failures the tool must catch rather than trust
fresh
check "a write that does not read back as written: exit 8" 8 "$(TROPICCTL_FAKE_CORRUPT_WRITE=1 rc mem write 20 --in in.bin)"
check_contains "and the message says so" "does not read back as written" "$(cat err.txt)"
fresh; T mem write 21 --in in.bin >/dev/null 2>&1
check "an erase that did not take effect: exit 8" 8 "$(TROPICCTL_FAKE_SILENT_ERASE=1 rc mem erase 21)"
check_contains "and the message says so" "still reads as populated" "$(cat err.txt)"

# ------------------------------------------------------------------ random
fresh
check "rng get 16" 0 "$(rc rng get 16)"
check "16 bytes as 32 hex digits" 32 "$(tr -d '\n' < out.txt | wc -c | tr -d ' ')"
check "two calls differ" yes "$(a=$(T rng get 16 2>/dev/null); b=$(T rng get 16 2>/dev/null); [ "$a" != "$b" ] && echo yes || echo no)"
check "rng get 0 refused" 2 "$(rc rng get 0)"
check "rng get 256 refused" 2 "$(rc rng get 256)"
check "rng get 255" 0 "$(rc rng get 255)"
check "rng --out writes raw bytes" 0 "$(rc rng get 20 --out r.bin)"
check "20 raw bytes" 20 "$(wc -c < r.bin | tr -d ' ')"
check "--hex with --out refused" 2 "$(rc rng get 4 --out r.bin --hex)"
if [ "$HAVE_PY" = 1 ]; then rc --json rng get 8 >/dev/null; check "rng --json is valid" valid "$(json_ok out.txt)"; fi

# ------------------------------------------------------------------ config
fresh
check "config dump" 0 "$(rc config dump)"
check "54 lines: 27 R and 27 I objects" 54 "$(wc -l < out.txt | tr -d ' ')"
check_contains "START_UP as measured on the bench chip" "R TR01_CFG_START_UP 0xffffffef" "$(cat out.txt)"
check_contains "an access object" "R TR01_CFG_UAP_ECDSA_SIGN 0xffffffff" "$(cat out.txt)"
if [ "$HAVE_PY" = 1 ]; then rc --json config dump >/dev/null; check "config dump --json is valid" valid "$(json_ok out.txt)"; fi

# ------------------------------------------------------------------ access rights (the fake's rule is ASSUMED, see chip_fake.c)
fresh; setkey 1
sed -i 's/^TR01_CFG_UAP_R_MEM_DATA_READ .*/TR01_CFG_UAP_R_MEM_DATA_READ 0xfffffffe/' chip/rconfig
check "[assumed rule] the shared key is denied R-MEM reads once its bit is cleared" 5 "$(rc --key shared mem read 1)"
check_contains "with the chip's own wording" "LT_L3_UNAUTHORIZED" "$(cat err.txt)"
printf 'x' > one.bin; T mem write 1 --in one.bin >/dev/null 2>&1
check "the per-unit key still reads" 0 "$(rc --key unit mem read 1)"
sed -i 's/^TR01_CFG_UAP_R_CONFIG_READ .*/TR01_CFG_UAP_R_CONFIG_READ 0xfffffffe/' chip/rconfig
check "config dump denied for the shared key" 5 "$(rc --key shared config dump)"
if [ "$HAVE_PY" = 1 ]; then
	rc --json --key shared config dump >/dev/null
	tail -n 1 err.txt > errlast.txt # notices are plain lines; the error object is the last line
	check "a JSON error on stderr" valid "$(json_ok errlast.txt)"
	check_contains "with the exit-code name" '"name":"denied"' "$(cat err.txt)"
fi

# ------------------------------------------------------------------ chip down, lock
fresh
touch chip/down
check "chip down: exit 3" 3 "$(rc info chip)"
check_contains "and says so" "fake chip is down" "$(cat err.txt)"
rm -f chip/down
if command -v flock >/dev/null 2>&1; then
	flock -x "$WORK/lock" sleep 4 & HOLD=$!
	sleep 0.5
	check "chip busy: exit 3 after the lock timeout" 3 "$(rc --lock-timeout 1 info chip)"
	check_contains "the message names the lock" "chip is busy" "$(cat err.txt)"
	check "--no-lock ignores the lock" 0 "$(rc --no-lock info chip)"
	wait "$HOLD" 2>/dev/null
	check "free again after the holder exits" 0 "$(rc --lock-timeout 1 info chip)"
else
	echo "SKIP  lock tests (no flock)"
fi

# ------------------------------------------------------------------ legacy lt-util mode
fresh
check "lt-util -i" 0 "$(lrc -i)"
check_contains "prints the CHIP_ID block" "CHIP_ID ver" "$(cat out.txt)"
check_contains "announces the key like the old tool" "no per-unit pairing key" "$(cat err.txt)"
check "lt-util with no arguments prints usage and exits 0" 0 "$(lrc)"
check_contains "usage mentions -m -s" "-m -s <slot> <file>" "$(cat out.txt)"
check "lt-util -m -s" 0 "$(lrc -m -s 3 in.bin)"
check "lt-util -m -r" 0 "$(lrc -m -r 3 got3.bin)"
check "round trip" yes "$(cmp -s in.bin got3.bin && echo yes || echo no)"
check "lt-util -m -s on an occupied slot fails with 1" 1 "$(lrc -m -s 3 in.bin)"
check "lt-util -m -e" 0 "$(lrc -m -e 3)"
check "lt-util -m -r on an empty slot fails with 1 (not 6)" 1 "$(lrc -m -r 3 got3.bin)"
check_contains "with the chip's wording" "LT_L3_R_MEM_DATA_READ_SLOT_EMPTY" "$(cat err.txt)"
check "lt-util -r 8 file" 0 "$(lrc -r 8 rr.bin)"
check "8 bytes" 8 "$(wc -c < rr.bin | tr -d ' ')"
check "lt-util -r 1 is refused like the old tool (count must be 2-255)" 1 "$(lrc -r 1 rr.bin)"
check "lt-util -k -r 0 file" 0 "$(lrc -k -r 0 lp0.bin)"
check "32 bytes" 32 "$(wc -c < lp0.bin | tr -d ' ')"
check "lt-util -k -r on an empty slot" 1 "$(lrc -k -r 1 lp1.bin)"
check "lt-util -x -f" 0 "$(lrc -x -f)"
check_contains "firmware line as before" "RISCV_FW_VER 00 02 01 00" "$(cat out.txt)"
check "lt-util -x -r" 0 "$(lrc -x -r)"
check "54 lines" 54 "$(wc -l < out.txt | tr -d ' ')"
check "an unknown old command fails with 1" 1 "$(lrc -z -z)"
check_contains "and says so" "wrong parameters" "$(cat err.txt)"
check "the deprecation notice appears unless silenced" yes "$(TROPICCTL_NO_DEPRECATION="" TROPICCTL_ARGV0=lt-util "$BIN" -x -f 2>&1 >/dev/null | grep -q 'compatibility mode' && echo yes || echo no)"
check "LT_UTIL_FORCE_SHARED_KEY selects the shared key" 0 "$(setkey 1; LT_UTIL_FORCE_SHARED_KEY=1 lrc -m -r 3 x.bin >/dev/null; rm -f chip/rmem/3; LT_UTIL_FORCE_SHARED_KEY=1 TROPICCTL_ARGV0=lt-util "$BIN" -m -s 3 in.bin >/dev/null 2>&1; echo $?)"
fresh; setkey 1
check "legacy: per-unit key in use" 0 "$(lrc -m -s 4 in.bin)"
check_contains "message the bench looks for" "per-unit pairing key, slot 1" "$(cat err.txt)"
rm -f chip/pairing/1
check "legacy: a key the chip does not know fails with 1" 1 "$(lrc -m -r 4 x.bin)"
check_contains "with the old 'Error sec channel' phrase" "Error sec channel" "$(cat err.txt)"
head -c 5 /dev/urandom > keys/pairing.pub
check "legacy: unusable key files fail with 1" 1 "$(lrc -m -r 4 x.bin)"
check_contains "and say 'unusable'" "unusable" "$(cat err.txt)"

# shellcheck source=cases_phase2.sh
. "$HERE/cases_phase2.sh"
# shellcheck source=cases_fw.sh
. "$HERE/cases_fw.sh"

finish
