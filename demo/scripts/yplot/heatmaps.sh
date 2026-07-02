#!/bin/bash
# yplot tool — 2D field heatmaps.
#
# When an expression references `y` (the pixel's vertical coordinate) it is a
# 2D field f(x,y); yplot renders it as a colormapped heatmap (perceptually
# uniform viridis) instead of a line curve. The field value is assumed to lie
# in [-1, 1] — scale your expression (or wrap it in tanh) to fit.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/heatmaps.sh

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

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== yplot — 2D field heatmaps ===\n\n'
p

# (1) Separable interference: sin(x)·cos(y) — the classic checkerboard of a
# standing wave on a membrane.
printf '(1) Standing-wave checkerboard  sin(x)·cos(y):\n'
"$YPLOT" -w 360 -H 360 --xrange=-6.28..6.28 --yrange=-6.28..6.28 \
    'field=sin(x)*cos(y)'
p

# (2) Concentric ripples from a point source: a function of radius only,
# sin(3r). Drop a stone in a pond.
printf '\n(2) Radial ripples  sin(3·sqrt(x²+y²)):\n'
"$YPLOT" -w 360 -H 360 --xrange=-6..6 --yrange=-6..6 \
    'field=sin(3*sqrt(x*x+y*y))'
p

# (3) Two-source interference: ripples from (±2,0) summed. Constructive and
# destructive fringes — the double-slit pattern.
printf '\n(3) Two-source interference (double slit):\n'
"$YPLOT" -w 360 -H 360 --xrange=-7..7 --yrange=-7..7 \
    'field=0.5*(sin(4*sqrt((x-2)*(x-2)+y*y))+sin(4*sqrt((x+2)*(x+2)+y*y)))'
p

# (4) A hyperbolic saddle, tanh(x·y): the sign structure of a 2D saddle point
# (positive in two opposing quadrants, negative in the other two).
printf '\n(4) Hyperbolic saddle  tanh(x·y):\n'
"$YPLOT" -w 360 -H 360 --xrange=-4..4 --yrange=-4..4 \
    'field=tanh(x*y)'
p

# (5) Procedural terrain via fBm: three octaves of noise2 at decorrelated
# (non-integer) frequencies. Summing octaves buries the axis-aligned grid
# structure of single-octave value noise and reads as organic terrain.
printf '\n(5) Procedural terrain  fBm(noise2):\n'
"$YPLOT" -w 360 -H 360 --xrange=0..4 --yrange=0..4 \
    'field=(0.55*noise2(x,y)+0.3*noise2(x*2.13,y*2.13)+0.15*noise2(x*4.27,y*4.27))*2-1'

printf '\n=== done — holding open ===\n'
