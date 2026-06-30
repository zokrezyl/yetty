#!/bin/bash
# yimage — basic showcase. Runs ycat on the raster assets in demo/assets/yimage/;
# ycat's image handler decodes PNG (and JPEG/GIF/BMP) and uploads it as a yimage
# figure that scrolls into the host yetty terminal's ydraw layer.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yimage/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/yimage"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yimage basics ===\n\n'
p

echo '$ ycat demo/assets/yimage/gradient.png             # synthetic gradient'
"$YCAT" "$ASSETS/gradient.png"
p

echo
echo '$ ycat demo/assets/yimage/rose.png                  # a small photograph'
"$YCAT" "$ASSETS/rose.png"
p

echo
echo '$ ycat demo/assets/yimage/wordmark.png              # alpha transparency'
"$YCAT" "$ASSETS/wordmark.png"
p

echo
echo '$ ycat -w 24 demo/assets/yimage/rose.png            # narrowed to 24 cells'
"$YCAT" -w 24 "$ASSETS/rose.png"

printf '\n=== done ===\n'
