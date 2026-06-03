#!/bin/bash
# Run result-checker on all yetty C files.
#
# Any extra arguments are forwarded to the checker, so the second mode can be
# enabled per invocation:
#
#   ./run-result-checker.sh                                   # propagation check (default)
#   ./run-result-checker.sh --check-double-eval               # both checks
#   ./run-result-checker.sh --check-double-eval --check-propagation=false  # double-eval only

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
YETTY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${YETTY_ROOT}/build-desktop-ytrace-release"
CHECKER="${BUILD_DIR}/qa-tools/analysis/result-checker/result-checker"

if [ ! -x "$CHECKER" ]; then
    echo "Error: result-checker not found at $CHECKER"
    echo "Build it first: make build-desktop-ytrace-release"
    exit 1
fi

if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
    echo "Error: compile_commands.json not found"
    exit 1
fi

find "${YETTY_ROOT}/src/yetty" -name "*.c" -type f | \
    xargs "$CHECKER" -p "$BUILD_DIR" "$@" 2>&1
