#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
LIB="${BUILD_DIR}/libfdhunt.so"

if [[ ! -f "$LIB" ]]; then
     echo "ERROR: ${LIB} missing.Run make first." >&2
     exit 1
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; exit 1; }

echo "----- Running libfdhunt Test Suite ------"


# Test 1: Clean execution (all descriptors closed)

echo "Test 1: Clean process execution"
OUTPUT=$(LD_PRELOAD="${LIB}" "${BUILD_DIR}/test_baseline" 2>&1 >/dev/null)

if echo "$OUTPUT" | grep -q "No leaked file descriptors detected."; then
    pass "Clean execution correctly reported zero leaks."
else
    echo "$OUTPUT"
    fail "Clean test reported leaks or failed to report clean state."
fi


# Test 2: Intentional leak detection

echo "Test 2: Leaked descriptor reporting"
OUTPUT=$(LD_PRELOAD="${LIB}" "${BUILD_DIR}/test_leak" 2>&1 >/dev/null || true)

if echo "$OUTPUT" | grep -q "Total leaked file descriptors found:" && \
   echo "$OUTPUT" | grep -q "Leaked file descriptor: fd="; then
    pass "Leak test caught and itemized unclosed descriptors."
else
    echo "$OUTPUT"
    fail "Leak test failed to identify leaked descriptors."
fi


# Test 3: Multi-threaded concurrency & mutex safety

echo "Test 3: Multi-threaded race condition check"
OUTPUT=$(LD_PRELOAD="${LIB}" "${BUILD_DIR}/test_mutex" 2>&1 >/dev/null || true)

if echo "$OUTPUT" | grep -q "No leaked file descriptors detected."; then
    pass "Multi-threaded test completed without race conditions or leaks."
else
    echo "$OUTPUT"
    fail "Multi-threaded test reported table inconsistency or race."
fi


# Test 4: External Linux binary smoke test

echo "Test 4: External system binary interposition (/bin/ls)"
if LD_PRELOAD="${LIB}" /bin/ls "${ROOT_DIR}" /dev/null 2>&1; then
    pass "System binary (/bin/ls) ran cleanly without crashing."
else
    fail "System binary (/bin/ls) crashed under LD_PRELOAD."
fi

echo -e "\n${GREEN}All test cases passed successfully.${NC}"