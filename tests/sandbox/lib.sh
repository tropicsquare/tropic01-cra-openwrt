# Shared helpers for the sandbox tests. Source this; do not run it.
#
# These tests run the router-side shell scripts on a normal Linux host with fake `logread`,
# `lt-util`, `cryptsetup` and friends, so their logic and safety rules can be checked without a board.
# They do NOT test the crypto or the hardware: see tests/hardware for that.

# shellcheck disable=SC2034  # REPO and WORK are used by the scripts that source this file
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
FAILS=0
CHECKS=0
trap 'cleanup_children; rm -rf "$WORK"' EXIT

# Processes started by a test (so a failing test never leaves a loop running).
CHILDREN=""
track() { CHILDREN="$CHILDREN $1"; }
cleanup_children() { for p in $CHILDREN; do kill "$p" 2>/dev/null || true; done; }

# check <name> <expected> <actual>: exact string comparison
check() {
	CHECKS=$((CHECKS + 1))
	if [ "$2" = "$3" ]; then
		printf 'PASS  %s\n' "$1"
	else
		FAILS=$((FAILS + 1))
		printf 'FAIL  %s\n        expected: %s\n        actual:   %s\n' "$1" "$2" "$3"
	fi
}
# check_contains <name> <needle> <haystack>
check_contains() {
	CHECKS=$((CHECKS + 1))
	case "$3" in
	*"$2"*) printf 'PASS  %s\n' "$1" ;;
	*) FAILS=$((FAILS + 1)); printf 'FAIL  %s\n        missing: %s\n        in:      %s\n' "$1" "$2" "$3" ;;
	esac
}
# check_absent <name> <needle> <haystack>
check_absent() {
	CHECKS=$((CHECKS + 1))
	case "$3" in
	*"$2"*) FAILS=$((FAILS + 1)); printf 'FAIL  %s\n        unexpected: %s\n' "$1" "$2" ;;
	*) printf 'PASS  %s\n' "$1" ;;
	esac
}

finish() {
	printf '%s: %d checks, %d failed\n' "$(basename "$0")" "$CHECKS" "$FAILS"
	[ "$FAILS" -eq 0 ]
}

skip() { printf 'SKIP  %s\n' "$1"; trap - EXIT; rm -rf "$WORK"; exit 0; }
