#!/bin/bash
# Run every sandbox test. Exit non-zero if any fails.
cd "$(dirname "$0")" || exit 1
rc=0
for t in audit.sh recorder.sh vault.sh; do
	echo "=== $t"
	./"$t" || rc=1
done
[ "$rc" -eq 0 ] && echo "ALL SANDBOX TESTS PASSED" || echo "SANDBOX TESTS FAILED"
exit "$rc"
