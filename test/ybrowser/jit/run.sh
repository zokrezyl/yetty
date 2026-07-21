#!/usr/bin/env bash
# Baseline-JIT test runner. From the repo root:
#   test/ybrowser/jit/run.sh [build-dir]
#
# Runs, against a build that has the JIT compiled in (Linux x86-64):
#   1. jit-selftest        — interp / eager / baseline agree on ~70 cases
#   2. jit-stress          — differential + 200 create/eval/destroy cycles
#   3. sanitized selftest  — ASan + UBSan (memory / UB / leaks)
#   4. sanitized stress    — ASan + UBSan
#
# The sanitized builds compile the qjs sources directly; the plain builds
# link the already-built static library.
set -u
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="${1:-$ROOT/build-desktop-ytrace-release}"
Q="$ROOT/src/quickjs"
TMP="$ROOT/tmp"
LIB="$BUILD/libqjs.a"
CC="${CC:-cc}"
fail=0

mkdir -p "$TMP"
# Guard every run with a timeout so an interruptibility/hang regression
# fails loudly instead of wedging CI.
run() { echo "=== $1 ==="; shift; timeout 120 "$@"; rc=$?;
        [ $rc -eq 124 ] && echo "TIMEOUT (possible hang / non-interruptible loop)";
        [ $rc -eq 0 ] || fail=1; echo; }

if [ ! -f "$LIB" ]; then
    echo "SKIP: $LIB not found (build the JIT-enabled target first)"; exit 0
fi

# --- plain (linked against libqjs.a) ---
$CC -O1 -I "$Q" -o "$TMP/jit-selftest" "$ROOT/test/ybrowser/jit/jit-selftest.c" \
    "$LIB" -lm -ldl -lpthread -lrt && run "jit-selftest" "$TMP/jit-selftest"
$CC -O1 -I "$Q" -o "$TMP/jit-stress" "$ROOT/test/ybrowser/jit/jit-stress.c" \
    "$LIB" -lm -ldl -lpthread -lrt && run "jit-stress" "$TMP/jit-stress" 200

# --- sanitized (direct source) ---
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
SRCS="$Q/quickjs.c $Q/dtoa.c $Q/libregexp.c $Q/libunicode.c $Q/sljit/sljit_src/sljitLir.c"
DEFS="-DQJS_ENABLE_JIT -DSLJIT_CONFIG_AUTO=1 -D_GNU_SOURCE -I $Q -I $Q/sljit/sljit_src"
# shellcheck disable=SC2086
$CC -O1 -g $SAN $DEFS -o "$TMP/jit-selftest-asan" \
    "$ROOT/test/ybrowser/jit/jit-selftest.c" $SRCS -lm -ldl -lpthread \
    && run "jit-selftest (ASan+UBSan)" env ASAN_OPTIONS=detect_leaks=1 "$TMP/jit-selftest-asan"
# shellcheck disable=SC2086
$CC -O1 -g $SAN $DEFS -o "$TMP/jit-stress-asan" \
    "$ROOT/test/ybrowser/jit/jit-stress.c" $SRCS -lm -ldl -lpthread \
    && run "jit-stress (ASan+UBSan)" env ASAN_OPTIONS=detect_leaks=1 "$TMP/jit-stress-asan" 100

if [ $fail -ne 0 ]; then echo "JIT TESTS FAILED"; exit 1; fi
echo "ALL JIT TESTS PASSED"
