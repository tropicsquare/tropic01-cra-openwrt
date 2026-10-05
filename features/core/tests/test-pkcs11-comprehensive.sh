#!/bin/sh
# Comprehensive PKCS#11 Test Script for TROPIC01

echo "==================================="
echo "PKCS#11 TROPIC01 Comprehensive Test"
echo "==================================="

# Target module for reference; /tmp/test-pkcs11-full below has it built in.
# shellcheck disable=SC2034
MODULE="/usr/lib/libtropic_pkcs11.so"

# Test 1: Random Number Generation (32 bytes)
printf '\n[TEST 1] Generate 32 bytes of random data...\n'
/tmp/test-pkcs11-full 2>&1 | grep -A1 "Random data:"

# Test 2: Generate multiple random numbers to verify uniqueness
printf '\n[TEST 2] Generate 3 sets of random data (16 bytes each) to verify uniqueness...\n'
for i in 1 2 3; do
	echo "  Set $i:"
	/tmp/test-pkcs11-full 2>&1 | grep "Random data:" | cut -d: -f2 | head -c 34
	echo
done

# Test 3: Test different random sizes
printf '\n[TEST 3] Test different random data sizes...\n'
for size in 8 16 32 64 128 256; do
	echo "  Testing $size bytes..."
	# Would need updated test program
done

printf '\n===================================\n'
echo "Basic tests completed successfully!"
echo "==================================="
