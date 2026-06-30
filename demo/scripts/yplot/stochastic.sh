#!/bin/bash
# yplot tool — random and noise.
#
# Showcases the stochastic opcodes: rand(x) hashes its argument to white
# noise in [0,1), and noise(x) is smooth value noise (interpolated lattice
# hash). Both are deterministic — the same argument always gives the same
# value — so the curves are stable frame to frame.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/stochastic.sh

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

printf '=== yplot — random & noise ===\n\n'
p

# (1) White noise vs smooth value noise at the same frequency. rand() jumps
# every sample; noise() glides between lattice points.
printf '(1) White noise  rand()  vs smooth  noise():\n'
"$YPLOT" -w 600 -H 240 --xrange=0..10 --yrange=-0.1..1.1 \
    'white=rand(x*8)' \
    'smooth=noise(x*8)' \
    '@white.color=#556162' \
    '@smooth.color=#74C5A5'
p

# (2) Fractal (fBm) noise: octaves of value noise at halving amplitude and
# doubling frequency — the standard recipe for natural-looking 1/f signals.
printf '\n(2) Fractal noise (summed octaves):\n'
"$YPLOT" -w 600 -H 240 --xrange=0..6 --yrange=-0.1..1.1 \
    'fbm=0.5*noise(x*2)+0.25*noise(x*4)+0.125*noise(x*8)+0.0625*noise(x*16)' \
    '@fbm.color=#6BA892'
p

# (3) A clean signal corrupted by additive noise — what every real-world
# sensor trace looks like before filtering.
printf '\n(3) Signal + additive noise:\n'
"$YPLOT" -w 600 -H 240 --xrange=0..6.28 --yrange=-1.3..1.3 \
    'clean=sin(x)' \
    'noisy=sin(x)+0.2*(rand(x*97)*2-1)' \
    '@clean.color=#364A47' \
    '@noisy.color=#FF6B6B'

printf '\n=== done — holding open ===\n'
