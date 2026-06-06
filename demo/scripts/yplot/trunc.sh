#!/bin/bash
# yplot tool — trunc, rounding toward zero.
#
# Showcases the built-in `trunc` opcode: drop the fractional part, rounding
# toward zero (so trunc(-2.7) = -2, not -3). Contrasted with floor/round/ceil
# and used to build quantizers and sawtooths.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/trunc.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YPLOT="${YPLOT:-$ROOT/build-desktop-ytrace-release/tools/yplot/yplot}"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YPLOT" ]; then
    echo "yplot binary not found at $YPLOT" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yplot — trunc, rounding toward zero ===\n\n'
p

# (1) The rounding family side by side. They agree for positive x, but for
# negative x trunc steps toward zero while floor steps down — watch them
# split left of the origin.
printf '(1) trunc vs floor vs round near the origin:\n'
"$YPLOT" -w 600 -H 240 --xrange=-3..3 --yrange=-3.5..3.5 \
    'truncated=trunc(x)' \
    'floored=floor(x)' \
    'rounded=round(x)' \
    '@truncated.color=#74C5A5' \
    '@floored.color=#FF6B6B' \
    '@rounded.color=#FFE66D'
p

# (2) A symmetric sawtooth. x - trunc(x) keeps the sign of x, giving a
# sawtooth that mirrors about the origin — different from fract(), which
# always climbs from 0.
printf '\n(2) Signed sawtooth: x - trunc(x) vs fract(x):\n'
"$YPLOT" -w 600 -H 240 --xrange=-3..3 --yrange=-1.1..1.1 \
    'signed_saw=x-trunc(x)' \
    'fract_saw=fract(x)' \
    '@signed_saw.color=#6BA892' \
    '@fract_saw.color=#556162'
p

# (3) A quantizer / ADC. trunc(signal * levels) / levels snaps a smooth sine
# onto discrete steps — the staircase a low-bit-depth converter produces.
printf '\n(3) Quantizer — sine snapped to discrete levels:\n'
"$YPLOT" -w 600 -H 240 --xrange=0..6.28 --yrange=-1.1..1.1 \
    'signal=sin(x)' \
    'quantized=trunc(sin(x)*4)/4' \
    '@signal.color=#364A47' \
    '@quantized.color=#74C5A5'
p

# (4) A bit-crushed ramp: trunc(x * k) / k holds each level for 1/k of the
# input before stepping — the sample-and-hold staircase of a bit-crusher.
printf '\n(4) Bit-crushed ramp:\n'
"$YPLOT" -w 600 -H 240 --xrange=0..4 --yrange=-0.2..4.2 \
    'ramp=x' \
    'crushed=trunc(x*3)/3' \
    '@ramp.color=#556162' \
    '@crushed.color=#6BA892'

printf '\n=== done — holding open ===\n'
