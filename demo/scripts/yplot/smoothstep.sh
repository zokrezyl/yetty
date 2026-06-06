#!/bin/bash
# yplot tool — smoothstep, the cubic ease curve.
#
# Showcases the built-in `smoothstep(edge0, edge1, x)` opcode: 0 below edge0,
# 1 above edge1, and a cubic Hermite ramp between them with zero slope at
# both ends. The workhorse easing / soft-threshold function of animation and
# procedural shading.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/smoothstep.sh

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

printf '=== yplot — smoothstep easing ===\n\n'
p

# (1) Ease in/out versus a linear ramp. Same endpoints, but smoothstep
# starts and ends flat — the difference between a mechanical slide and a
# natural-looking one.
printf '(1) smoothstep ease vs linear ramp:\n'
"$YPLOT" -w 600 -H 240 --xrange=0..1 --yrange=-0.1..1.1 \
    'linear=x' \
    'eased=smoothstep(0,1,x)' \
    '@linear.color=#556162' \
    '@eased.color=#74C5A5'
p

# (2) Feed smoothstep into itself to steepen the transition (the classic
# "smootherstep" trick): each pass flattens the ends further.
printf '\n(2) Iterated smoothstep — steeper transitions:\n'
"$YPLOT" -w 600 -H 240 --xrange=0..1 --yrange=-0.1..1.1 \
    'once=smoothstep(0,1,x)' \
    'twice=smoothstep(0,1,smoothstep(0,1,x))' \
    '@once.color=#5A8979' \
    '@twice.color=#74C5A5'
p

# (3) A soft window: ramp up across one edge, back down across another. The
# antialiased band-pass / spotlight falloff used everywhere in shaders.
printf '\n(3) Soft window — ramp up then down:\n'
"$YPLOT" -w 600 -H 240 --xrange=-4..4 --yrange=-0.1..1.1 \
    'gate=smoothstep(-2,-1,x)*(1 - smoothstep(1,2,x))' \
    '@gate.color=#6BA892'
p

# (4) A soft staircase: integer steps with smoothstep-blended risers instead
# of vertical jumps. Compare to the hard floor() staircase.
printf '\n(4) Soft staircase vs hard floor():\n'
"$YPLOT" -w 600 -H 240 --xrange=0..5 --yrange=-0.2..5.2 \
    'hard=floor(x)' \
    'soft=floor(x)+smoothstep(0,1,fract(x))' \
    '@hard.color=#364A47' \
    '@soft.color=#74C5A5'
p

# (5) A contrast curve: smoothstep between two interior edges remaps a
# 0..1 signal, darkening shadows and brightening highlights — the tone
# response of a contrast slider.
printf '\n(5) Contrast remap (S-curve tone response):\n'
"$YPLOT" -w 600 -H 240 --xrange=0..1 --yrange=-0.1..1.1 \
    'identity=x' \
    'contrast=smoothstep(0.3,0.7,x)' \
    '@identity.color=#556162' \
    '@contrast.color=#FFE66D'

printf '\n=== done — holding open ===\n'
