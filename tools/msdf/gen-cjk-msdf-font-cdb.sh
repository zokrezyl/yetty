#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CACHE_DIR="${HOME}/.cache/yetty/msdf-font-cache"
GEN_TOOL="$PROJECT_DIR/build-desktop-dawn-debug/src/yetty/msdf-gen/yetty-msdf-gen"

# Default CJK font (Noto Sans Mono CJK - good for terminals)
CJK_FONT="${CJK_FONT:-/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc}"

if [ ! -x "$GEN_TOOL" ]; then
    echo "Error: yetty-msdf-gen not found at $GEN_TOOL"
    echo "Build first: cmake --build build-desktop-dawn-debug"
    exit 1
fi

if [ ! -f "$CJK_FONT" ]; then
    echo "Error: CJK font not found at $CJK_FONT"
    echo "Install with: apt install fonts-noto-cjk"
    echo "Or set CJK_FONT environment variable to a different font"
    exit 1
fi

mkdir -p "$CACHE_DIR"

echo "=== Generating CJK MSDF CDB ==="
echo "Font: $CJK_FONT"
echo "Output: $CACHE_DIR"
echo ""

# Generate all glyphs from the CJK font
"$GEN_TOOL" --all "$CJK_FONT" "$CACHE_DIR"

echo ""
echo "Done. CDB files in $CACHE_DIR"
ls -lh "$CACHE_DIR"/*.cdb 2>/dev/null || true
