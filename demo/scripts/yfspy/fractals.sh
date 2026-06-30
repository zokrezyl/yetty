#!/bin/bash
# yfspy tool — escape-time fractals from Python-subset shaders.
#
# These shaders read both x and y, so they are 2D fields f(x,y); yfspy flags
# them and yetty renders a colormapped heatmap. The data-dependent escape
# loops are exactly what yfsvm's branch/jump opcodes make possible. The
# compile step needs `uv` on PATH.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yfspy/fractals.sh

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

p() { sleep "$PAUSE"; }

printf '=== yfspy — escape-time fractals (heatmap fields) ===\n\n'
p

# (1) The Mandelbrot set: cx, cy bind positionally to (x, y). The classic
# cardioid + bulb, framed over the standard view.
printf '(1) mandelbrot.py — escape iterations as a heatmap:\n'
"$YFSPY" "$ASSETS/mandelbrot.py" -w 480 -H 360 \
    --xrange=-2.5..1 --yrange=-1.2..1.2 --no-grid --no-axes
p

# (2) A Julia set for c = -0.8 + 0.156i — same iteration, but x,y seed z0
# while c is fixed, giving a different field.
printf '\n(2) julia.py — Julia set for a fixed constant:\n'
"$YFSPY" "$ASSETS/julia.py" -w 480 -H 360 \
    --xrange=-1.8..1.8 --yrange=-1.3..1.3 --no-grid --no-axes

printf '\n=== done ===\n'
