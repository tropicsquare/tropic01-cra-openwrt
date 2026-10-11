# Test helpers for tropicctl. Source this; do not run it. Self-contained so the tool can move to its own repository.
FAILS=0
CHECKS=0
check() { # check <name> <expected> <actual>
	CHECKS=$((CHECKS + 1))
	if [ "$2" = "$3" ]; then printf 'PASS  %s\n' "$1"; else
		FAILS=$((FAILS + 1)); printf 'FAIL  %s\n        expected: %s\n        actual:   %s\n' "$1" "$2" "$3"
	fi
}
check_contains() { # check_contains <name> <needle> <haystack>
	CHECKS=$((CHECKS + 1))
	case "$3" in *"$2"*) printf 'PASS  %s\n' "$1" ;; *) FAILS=$((FAILS + 1)); printf 'FAIL  %s\n        missing: %s\n        in:      %s\n' "$1" "$2" "$3" ;; esac
}
check_absent() { # check_absent <name> <needle> <haystack>
	CHECKS=$((CHECKS + 1))
	case "$3" in *"$2"*) FAILS=$((FAILS + 1)); printf 'FAIL  %s\n        unexpected: %s\n' "$1" "$2" ;; *) printf 'PASS  %s\n' "$1" ;; esac
}
finish() {
	printf '%s: %d checks, %d failed\n' "$(basename "$0")" "$CHECKS" "$FAILS"
	[ "$FAILS" -eq 0 ]
}
