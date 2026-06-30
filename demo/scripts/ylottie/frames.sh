#!/bin/bash
# ylottie — single-frame stepping. Instead of animating, render individual
# frames of one Lottie composition with --frame, so the still poses stack down
# the scrolling layer. Useful for inspecting interpolation without motion.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ylottie/frames.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YLOTTIE="${YLOTTIE:-$ROOT/build-desktop-ytrace-release/tools/ylottie/yetty-ylottie}"
ASSETS="$ROOT/demo/assets/thorvg"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YLOTTIE" ]; then
    YLOTTIE="$(command -v "${YLOTTIE##*/}" 2>/dev/null || true)"
fi
if [ -z "$YLOTTIE" ] || [ ! -x "$YLOTTIE" ]; then
    echo "yetty-ylottie binary not found in build dir or on \$PATH — set YLOTTIE=path/to/yetty-ylottie" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ylottie — frame stepping (bouncing-ball.json) ===\n\n'
p

for frame in 0 8 16 24 32 40; do
    echo "\$ yetty-ylottie --frame $frame --no-clear demo/assets/thorvg/bouncing-ball.json"
    "$YLOTTIE" --frame "$frame" --no-clear "$ASSETS/bouncing-ball.json"
    p
    echo
done

printf '=== done ===\n'
