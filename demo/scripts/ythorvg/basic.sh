#!/bin/bash
# ythorvg — basic showcase. Drives the standalone yetty-ythorvg bridge, which
# rasterises SVG and Lottie through ThorVG into ydraw OSC. Demonstrates both
# input modes: auto-detected SVG, and a forced-Lottie frame.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ythorvg/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YTHORVG="${YTHORVG:-$ROOT/build-desktop-ytrace-release/tools/ythorvg/yetty-ythorvg}"
SVG="$ROOT/demo/assets/svg"
TVG="$ROOT/demo/assets/thorvg"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YTHORVG" ]; then
    echo "yetty-ythorvg binary not found at $YTHORVG — set YTHORVG=path/to/yetty-ythorvg" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ythorvg — SVG + Lottie via ThorVG ===\n\n'
p

echo '$ yetty-ythorvg demo/assets/svg/tiger.svg          # SVG (auto-detected)'
"$YTHORVG" "$SVG/tiger.svg"
p

echo
echo '$ yetty-ythorvg demo/assets/thorvg/sunset.svg       # gradient-heavy SVG'
"$YTHORVG" "$TVG/sunset.svg"
p

echo
echo '$ yetty-ythorvg --lottie --frame 20 demo/assets/thorvg/bouncing-ball.json'
"$YTHORVG" --lottie --frame 20 "$TVG/bouncing-ball.json"
p

echo
echo '$ yetty-ythorvg --lottie --frame 15 demo/assets/thorvg/confetti.json'
"$YTHORVG" --lottie --frame 15 "$TVG/confetti.json"

printf '\n=== done ===\n'
