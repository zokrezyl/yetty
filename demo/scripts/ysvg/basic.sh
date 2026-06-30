#!/bin/bash
# ysvg — basic showcase. Runs ycat on the SVG assets in demo/assets/svg/; ycat's
# svg handler parses them into ydraw SDF + MSDF primitives that scroll into the
# host yetty terminal's ydraw layer.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/svg"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ysvg basics ===\n\n'
p

echo '$ ycat demo/assets/svg/svg-logo.svg               # gradients + paths'
"$YCAT" "$ASSETS/svg-logo.svg"
p

echo
echo '$ ycat demo/assets/svg/tiger.svg                   # the classic path-heavy tiger'
"$YCAT" "$ASSETS/tiger.svg"
p

echo
echo '$ ycat demo/assets/svg/smiley-green-alien.svg       # filled shapes'
"$YCAT" "$ASSETS/smiley-green-alien.svg"
p

echo
echo '$ ycat -c svg demo/assets/svg/de-flag.svg           # forced svg handler'
"$YCAT" -c svg "$ASSETS/de-flag.svg"

printf '\n=== done ===\n'
