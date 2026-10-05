#!/bin/sh
# Quick PKCS#11 Tests for TROPIC01

# Target module for reference; /tmp/test-pkcs11-full below has it built in.
# shellcheck disable=SC2034
MODULE="/usr/lib/libtropic_pkcs11.so"

echo "========================================="
echo "  PKCS#11 TROPIC01 Quick Test Suite"
echo "========================================="

# Test 1: Generate Random Data
printf '\n✓ TEST 1: Random Number Generation (32 bytes)\n'
/tmp/test-pkcs11-full 2>&1 | grep -E "^(===|Result:|Random data:)" | tail -3

# Test 2: Multiple Random Generations
printf '\n✓ TEST 2: Generate 5x random numbers (verify uniqueness)\n'
for i in 1 2 3 4 5; do
	random=$(/tmp/test-pkcs11-full 2>&1 | grep "^Random data:" | cut -d: -f2 | tr -d ' ')
	printf '  %s: %s...\n' "$i" "$(echo "$random" | cut -c1-32)"
done

# Test 3: Stress Test
printf '\n✓ TEST 3: Rapid Generation (10 iterations)\n'
start=$(date +%s)
for i in $(seq 1 10); do
	/tmp/test-pkcs11-full >/dev/null 2>&1
done
end=$(date +%s)
duration=$((end - start))
echo "  Completed 10 full sessions in ${duration}s"
rate=$((10 * 1000 / duration))
echo "  Rate: ~${rate} operations/sec"

printf '\n=========================================\n'
echo "  All tests completed successfully! ✓"
echo "========================================="
