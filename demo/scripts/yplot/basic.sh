#!/bin/bash
# yplot tool — basic showcase. Runs the standalone yplot binary on a few
# function expressions and lets the OSC envelopes scroll into the ydraw
# layer of the host yetty terminal.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YPLOT="${YPLOT:-$ROOT/build-desktop-ytrace-release/tools/yplot/yplot}"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YPLOT" ]; then
    YPLOT="$(command -v "${YPLOT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YPLOT" ] || [ ! -x "$YPLOT" ]; then
    echo "yplot binary not found in build dir or on \$PATH — set YPLOT=path/to/yplot" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== yplot tool basics ===\n\n'
p

# Single function — defaults for everything else.
echo '$ yplot "sin(x)"'
"$YPLOT" 'sin(x)'
p

# Multi-function with explicit names + per-plot color overrides. The names
# double as the legend labels, so pick descriptive ones.
echo
echo '$ yplot "sine=sin(x); cosine=cos(x); @sine.color=#FF6B6B; @cosine.color=#4ECDC4"'
"$YPLOT" 'sine=sin(x); cosine=cos(x); @sine.color=#FF6B6B; @cosine.color=#4ECDC4'
p

# Custom dimensions and axis range.
echo
echo '$ yplot -w 480 -H 240 --xrange=-3..3 --yrange=-2..10 "x*x" "2*x+1"'
"$YPLOT" -w 480 -H 240 --xrange=-3..3 --yrange=-2..10 'parabola=x*x' 'line=2*x+1' \
    '@parabola.color=#FFE66D' '@line.color=#AA96DA'
p

# Minimal: no grid, no axes, no labels.
echo
echo '$ yplot --no-grid --no-axes --no-labels "sin(x)*cos(3*x)"'
"$YPLOT" --no-grid --no-axes --no-labels 'sin(x)*cos(3*x)'
p

# Chained spectrum-like plot.
echo
echo 'audio harmonics:'
"$YPLOT" -w 520 -H 200 --xrange=0..6.28 --yrange=-1..1 \
    'first=sin(x)' \
    'second=sin(2*x)/2' \
    'third=sin(3*x)/3' \
    'sum=sin(x)+sin(2*x)/2+sin(3*x)/3' \
    '@sum.color=#FCBF49' \
    '@first.color=#FF6B6B' \
    '@second.color=#4ECDC4' \
    '@third.color=#AA96DA'

printf '\n=== done — holding open ===\n'
