#!/bin/bash
# yfspy tool — line plots from Python-subset shaders.
#
# yfspy compiles a restricted-Python shader (assets/yfspy/) to yfsvm bytecode
# client-side, then emits the same yplot OSC envelope as tools/yplot. Each
# top-level `def` becomes one curve. The compile step needs `uv` on PATH.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yfspy/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YFSPY="${YFSPY:-$ROOT/build-desktop-ytrace-release/tools/yfspy/yfspy}"
ASSETS="$ROOT/demo/assets/yfspy"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YFSPY" ]; then
    YFSPY="$(command -v "${YFSPY##*/}" 2>/dev/null || true)"
fi
if [ -z "$YFSPY" ] || [ ! -x "$YFSPY" ]; then
    echo "yfspy binary not found in build dir or on \$PATH" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== yfspy — Python-subset line plots ===\n\n'
p

# (1) Two curves from one shader file: an animated wave and a Gaussian-
# windowed oscillation. Each def in waves.py is a separate curve.
printf '(1) waves.py — two functions, one plot:\n'
"$YFSPY" "$ASSETS/waves.py" -w 600 -H 240 --xrange=-6.28..6.28 --yrange=-1..1
p

# (2) A square-wave Fourier partial sum computed with a real while-loop in the
# shader — the control flow yfsvm now supports. Without branch/jump opcodes
# this could only be a fixed unrolled expression.
printf '\n(2) series.py — square wave via an in-shader while-loop:\n'
"$YFSPY" "$ASSETS/series.py" -w 600 -H 240 --xrange=-6.28..6.28 --yrange=-1..1

printf '\n=== done ===\n'
