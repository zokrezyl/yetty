#!/bin/bash
# YSheet demo — load sample spreadsheet and render onto the yetty canvas.
#
# Usage:
#   ./demo.sh             dump-mode (one frame, then exit)
#   ./demo.sh --view      live loop, 'q' to quit

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

YSHEET="$PROJECT_ROOT/build-desktop-ytrace-release/tools/ysheet/ysheet"
ASSET="$PROJECT_ROOT/demo/assets/yrich/ysheet/sample.ysheet.yaml"

if [[ ! -x "$YSHEET" ]]; then
    YSHEET="$(command -v "${YSHEET##*/}" 2>/dev/null || true)"
fi
if [[ -z "$YSHEET" || ! -x "$YSHEET" ]]; then
    echo "Error: ysheet not found in build dir or on \$PATH" >&2
    echo "Build first: make build-desktop-ytrace-release" >&2
    exit 1
fi

if [[ ! -f "$ASSET" ]]; then
    echo "Error: demo asset not found at $ASSET" >&2
    exit 1
fi

if [[ "${1:-}" == "--view" ]]; then
    exec "$YSHEET" -f "$ASSET"
else
    exec "$YSHEET" -f "$ASSET" --dump
fi
