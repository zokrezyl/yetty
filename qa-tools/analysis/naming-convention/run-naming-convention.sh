#!/bin/bash
# Run naming-convention on all yetty C files

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
YETTY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${YETTY_ROOT}/build-desktop-ytrace-release"
CHECKER="${BUILD_DIR}/qa-tools/analysis/naming-convention/naming-convention"

if [ ! -x "$CHECKER" ]; then
    echo "Error: naming-convention not found at $CHECKER"
    echo "Build it first: make build-desktop-ytrace-release"
    exit 1
fi

if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
    echo "Error: compile_commands.json not found"
    exit 1
fi

find "${YETTY_ROOT}/src/yetty" -name "*.c" -type f | \
    xargs "$CHECKER" -p "$BUILD_DIR" 2>&1
