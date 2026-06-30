#!/bin/bash
# yplot tool — signal-shaping showcase.
#
# Demonstrates composing buffer inputs and pure-function expressions in one
# plot: an inline envelope buffer modulates several carriers, all rendered
# in the same draw call. Each curve gets its own colour.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/signal-shaping.sh

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

printf '=== yplot signal shaping ===\n\n'
p

# (1) ADSR-shaped envelope drives three differently-pitched carriers.
# The same `env` buffer is sampled by all three expression curves; the
# carrier is `sin(N*x)` for N ∈ {6, 12, 24}.
printf '(1) ADSR envelope × three harmonics:\n'
"$YPLOT" -w 560 -H 200 --xrange=0..1 --yrange=-1.1..1.1 \
    'env=buffer; @env.size=16; @env.values=0,0.4,0.8,1,0.95,0.85,0.75,0.65,0.55,0.45,0.35,0.25,0.18,0.12,0.06,0' \
    'h1=env(x)*sin(x*6)' \
    'h2=env(x)*sin(x*12)' \
    'h3=env(x)*sin(x*24)' \
    '@env.color=#364A47' \
    '@h1.color=#FF6B6B' \
    '@h2.color=#FFE66D' \
    '@h3.color=#74C5A5'
p

# (2) Two control buffers driving a parametric expression.
# `gain(x)` and `bias(x)` shape `sin(x*60) * gain + bias`. The two buffers
# are themselves rendered as faint reference curves.
printf '\n(2) gain + bias driven carrier:\n'
"$YPLOT" -w 560 -H 200 --xrange=0..1 --yrange=-1.5..1.5 \
    'gain=buffer; @gain.size=8; @gain.values=0.1,0.4,0.7,1,1,0.7,0.4,0.1' \
    'bias=buffer; @bias.size=8; @bias.values=0,0.05,0.1,0.15,0.1,0,-0.1,-0.05' \
    'out=sin(x*60)*gain(x)+bias(x)' \
    '@gain.color=#556162' \
    '@bias.color=#9FA7A8' \
    '@out.color=#6BA892'
p

# (3) Time-animated mix: the envelope is held static, the modulator's phase
# is driven by `time` so the resulting waveform travels across the window
# while the envelope stays put.
printf '\n(3) static envelope, travelling phase:\n'
"$YPLOT" -w 560 -H 200 --xrange=0..1 --yrange=-1.1..1.1 \
    'env=buffer; @env.size=12; @env.values=0,0.3,0.7,1,0.95,0.85,0.7,0.5,0.3,0.15,0.05,0' \
    'travel=env(x)*sin(x*40 - time*4)' \
    '@env.color=#5A8979' \
    '@travel.color=#74C5A5'

printf '\n=== done — holding open ===\n'
