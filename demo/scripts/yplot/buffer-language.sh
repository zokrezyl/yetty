#!/bin/bash
# yplot tool — buffer-input language showcase.
#
# Demonstrates the N-inputs / M-outputs plot model:
#   - `name=buffer` declares a named buffer-backed input
#   - `@name.size=N`            sets capacity (suffixes k/m supported)
#   - `@name.values=v1,v2,...`  initialises with inline values
#   - `name(x)` inside an expression samples that buffer at the current x
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/buffer-language.sh

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

printf '=== yplot buffer-input language ===\n\n'
p

# (1) Single inline buffer rendered as a curve. The buffer's samples are
# spread across the X domain by the shader's linear-interpolation walk.
printf '(1) inline buffer values (8 samples spread over xrange):\n'
"$YPLOT" -w 520 -H 160 --xrange=0..1 --yrange=-1..1 \
    'data=buffer; @data.size=8; @data.values=0,0.3,0.6,0.9,0.6,0,-0.4,-0.2'
p

# (2) Buffer + expression: the expression `pulse=data(x)*sin(x*8)` references
# `data` as a sampler. data(x) returns the buffer's value at the current x,
# multiplied by a high-frequency carrier.
printf '\n(2) buffer * sinusoidal carrier:\n'
"$YPLOT" -w 520 -H 160 --xrange=0..1 --yrange=-1..1 \
    'envelope=buffer; @envelope.size=8; @envelope.values=0,0.3,0.6,0.9,0.6,0,-0.4,-0.2' \
    'pulse=envelope(x)*sin(x*60)' \
    '@pulse.color=#74C5A5'
p

# (3) Two buffers acting as inputs to one expression. With samples mapped to
# x in lockstep, by(x)/bx(x) gives "y over x" without needing a scatter
# primitive.
printf '\n(3) y/x ratio of two buffers:\n'
"$YPLOT" -w 520 -H 160 --xrange=0..1 --yrange=0..6 \
    'bx=buffer; @bx.size=6; @bx.values=1,1.5,2,2.5,3,3.5' \
    'by=buffer; @by.size=6; @by.values=1,2.25,4,6.25,9,12.25' \
    'ratio=by(x)/bx(x)' \
    '@ratio.color=#FFE66D'
p

# (4) Animated buffer: the static buffer is amplitude-modulated by `time`.
# The plot auto-subscribes to the animation timer because `time` is
# referenced — try resizing the terminal to see the smooth update rate.
printf '\n(4) time-modulated buffer (animated):\n'
"$YPLOT" -w 520 -H 160 --xrange=0..1 --yrange=-1.2..1.2 \
    'wave=buffer; @wave.size=16; @wave.values=0,0.4,0.7,0.95,1,0.95,0.7,0.4,0,-0.4,-0.7,-0.95,-1,-0.95,-0.7,-0.4' \
    'live=wave(x)*(0.5+0.5*sin(time*2))' \
    '@live.color=#6BA892'

printf '\n=== done — holding open ===\n'
