#!/bin/bash
# YSlide demo — load sample 5-slide deck and render onto the yetty canvas.
#
# Usage:
#   ./demo.sh             dump-mode (slide 1, then exit)
#   ./demo.sh --view      live loop, Left/Right arrows to navigate, 'q' to quit

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

YSLIDE="$PROJECT_ROOT/build-desktop-ytrace-release/tools/yslide/yslide"
ASSET="$PROJECT_ROOT/demo/assets/yrich/yslide/sample.yslide.yaml"

if [[ ! -x "$YSLIDE" ]]; then
    YSLIDE="$(command -v "${YSLIDE##*/}" 2>/dev/null || true)"
fi
if [[ -z "$YSLIDE" || ! -x "$YSLIDE" ]]; then
    echo "Error: yslide not found in build dir or on \$PATH" >&2
    echo "Build first: make build-desktop-ytrace-release" >&2
    exit 1
fi

if [[ ! -f "$ASSET" ]]; then
    echo "Error: demo asset not found at $ASSET" >&2
    exit 1
fi

if [[ "${1:-}" == "--view" ]]; then
    exec "$YSLIDE" -f "$ASSET"
else
    exec "$YSLIDE" -f "$ASSET" --dump
fi
