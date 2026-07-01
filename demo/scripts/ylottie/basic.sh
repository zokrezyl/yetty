#!/bin/bash
# ylottie — basic showcase. Plays Lottie (Bodymovin JSON) animations as ydraw
# OSC through the standalone yetty-ylottie tool. Each clip animates in place in
# the host yetty terminal's ydraw layer; Ctrl-C moves to the next.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ylottie/basic.sh
#
# Set YLOTTIE_LOOP=1 to loop each animation forever (Ctrl-C to advance).

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YLOTTIE="${YLOTTIE:-$ROOT/build-desktop-ytrace-release/tools/ylottie/yetty-ylottie}"
ASSETS="$ROOT/demo/assets/thorvg"
PAUSE="${DEMO_PAUSE:-0}"
LOOP=""
[ "${YLOTTIE_LOOP:-0}" = "1" ] && LOOP="--loop"

if [ ! -x "$YLOTTIE" ]; then
    YLOTTIE="$(command -v "${YLOTTIE##*/}" 2>/dev/null || true)"
fi
if [ -z "$YLOTTIE" ] || [ ! -x "$YLOTTIE" ]; then
    echo "yetty-ylottie binary not found in build dir or on \$PATH — set YLOTTIE=path/to/yetty-ylottie" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ylottie basics ===\n\n'
p

echo '$ yetty-ylottie demo/assets/thorvg/bouncing-ball.json'
"$YLOTTIE" $LOOP "$ASSETS/bouncing-ball.json"
p

echo
echo '$ yetty-ylottie demo/assets/thorvg/confetti.json'
"$YLOTTIE" $LOOP "$ASSETS/confetti.json"
p

echo
echo '$ yetty-ylottie demo/assets/thorvg/animation-long.json'
"$YLOTTIE" $LOOP "$ASSETS/animation-long.json"

printf '\n=== done ===\n'
