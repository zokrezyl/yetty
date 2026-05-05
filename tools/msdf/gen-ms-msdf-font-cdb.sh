#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ASSETS_DIR="$PROJECT_DIR/assets"
CACHE_DIR="${HOME}/.cache/yetty/msdf-font-cache"
GEN_TOOL="$PROJECT_DIR/build-desktop-dawn-debug/src/yetty/msdf-gen/yetty-msdf-gen"

if [ ! -x "$GEN_TOOL" ]; then
    echo "Error: yetty-msdf-gen not found at $GEN_TOOL"
    echo "Build first: cmake --build build-desktop-dawn-debug"
    exit 1
fi

mkdir -p "$CACHE_DIR"

for style in Regular Bold Oblique BoldOblique; do
    ttf="$ASSETS_DIR/DejaVuSansMNerdFontMono-${style}.ttf"
    if [ ! -f "$ttf" ]; then
        echo "Skipping $style (no TTF found)"
        continue
    fi
    echo "=== Generating $style ==="
    "$GEN_TOOL" "$ttf" "$CACHE_DIR"
done

echo "Done. CDB files in $CACHE_DIR"
