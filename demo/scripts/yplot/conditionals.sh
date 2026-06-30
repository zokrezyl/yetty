#!/bin/bash
# yplot tool — comparisons and branchless select.
#
# Showcases the comparison opcodes (lt/gt/le/ge/eq/ne, each returning 1.0 or
# 0.0) and select(falseVal, trueVal, cond), which together build piecewise
# functions, gates, and domain guards without any branching.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/conditionals.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YPLOT="${YPLOT:-$ROOT/build-desktop-ytrace-release/tools/yplot/yplot}"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YPLOT" ]; then
    YPLOT="$(command -v "${YPLOT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YPLOT" ] || [ ! -x "$YPLOT" ]; then
    echo "yplot binary not found in build dir or on \$PATH" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yplot — comparisons & select ===\n\n'
p

# (1) A genuine piecewise function: a parabola for x<0, a sine for x>=0,
# stitched at the origin with select() + a comparison.
printf '(1) Piecewise  x<0 ? x² : sin(4x):\n'
"$YPLOT" -w 560 -H 240 --xrange=-3..3 --yrange=-1.2..2.2 \
    'piecewise=select(x*x, sin(4*x), ge(x,0))' \
    '@piecewise.color=#74C5A5'
p

# (2) A rectangular pulse: 1 where |x|<1, else 0 — built purely from a
# comparison. The indicator function / boxcar window.
printf '\n(2) Boxcar pulse  [|x| < 1]:\n'
"$YPLOT" -w 560 -H 240 --xrange=-3..3 --yrange=-0.2..1.2 \
    'boxcar=lt(abs(x),1)' \
    '@boxcar.color=#FFE66D'
p

# (3) ReLU and a hard step: the rectifier max(x,0) and the Heaviside step
# gt(x,0) — the two staples of neural-net and signal nonlinearities.
printf '\n(3) ReLU  max(x,0)  and Heaviside step:\n'
"$YPLOT" -w 560 -H 240 --xrange=-3..3 --yrange=-0.5..3 \
    'relu=max(x,0)' \
    'heaviside=gt(x,0)' \
    '@relu.color=#6BA892' \
    '@heaviside.color=#F38181'
p

# (4) A staircase from summed steps: each ge() adds a unit tread at a
# threshold. Comparisons compose into quantizers.
printf '\n(4) Threshold staircase (summed steps):\n'
"$YPLOT" -w 560 -H 240 --xrange=-3..3 --yrange=-0.2..4.2 \
    'stairs=ge(x,-2)+ge(x,-1)+ge(x,0)+ge(x,1)+ge(x,2)' \
    '@stairs.color=#74C5A5'

printf '\n=== done — holding open ===\n'
