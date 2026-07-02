#!/bin/bash
# YDoc demo — load sample document and render onto the yetty canvas.
#
# Usage:
#   ./demo.sh             dump-mode (one frame, then exit)
#   ./demo.sh --view      live loop, 'q' to quit

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

YDOC="$PROJECT_ROOT/build-desktop-ytrace-release/tools/ydoc/ydoc"
ASSET="$PROJECT_ROOT/demo/assets/yrich/ydoc/sample.ydoc.yaml"

if [[ ! -x "$YDOC" ]]; then
    YDOC="$(command -v "${YDOC##*/}" 2>/dev/null || true)"
fi
if [[ -z "$YDOC" || ! -x "$YDOC" ]]; then
    echo "Error: ydoc not found in build dir or on \$PATH" >&2
    echo "Build first: make build-desktop-ytrace-release" >&2
    exit 1
fi

if [[ ! -f "$ASSET" ]]; then
    echo "Error: demo asset not found at $ASSET" >&2
    exit 1
fi

if [[ "${1:-}" == "--view" ]]; then
    exec "$YDOC" -f "$ASSET"
else
    exec "$YDOC" -f "$ASSET" --dump
fi
