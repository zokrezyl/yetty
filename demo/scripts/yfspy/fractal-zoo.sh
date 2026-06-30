#!/bin/bash
# yfspy tool — a wider fractal zoo, beyond the basic Mandelbrot/Julia.
#
# Escape-time variants (Burning Ship, smooth-coloured Mandelbrot), a
# higher-degree Newton fractal (five root basins), and a Julia set coloured by
# an orbit trap rather than escape time. All are heatmap fields; the orbit-trap
# Julia animates via `time`. The compile step needs `uv` on PATH.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yfspy/fractal-zoo.sh

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

printf '=== yfspy — fractal zoo ===\n\n'
p

printf '(1) burning_ship.py — the Burning Ship fractal:\n'
"$YFSPY" "$ASSETS/burning_ship.py" -w 480 -H 360 \
    --xrange=-1.8..-1.7 --yrange=-0.08..0.01 --no-grid --no-axes
p

printf '\n(2) mandelbrot_smooth.py — continuous (smooth) escape coloring:\n'
"$YFSPY" "$ASSETS/mandelbrot_smooth.py" -w 480 -H 360 \
    --xrange=-2.2..0.8 --yrange=-1.2..1.2 --no-grid --no-axes
p

printf '\n(3) newton5.py — Newton fractal for z^5 - 1 (five basins):\n'
"$YFSPY" "$ASSETS/newton5.py" -w 420 -H 420 \
    --xrange=-1.5..1.5 --yrange=-1.5..1.5 --no-grid --no-axes
p

printf '\n(4) julia_orbit_trap.py — Julia coloured by a cross orbit trap:\n'
"$YFSPY" "$ASSETS/julia_orbit_trap.py" -w 480 -H 360 \
    --xrange=-1.6..1.6 --yrange=-1.2..1.2 --no-grid --no-axes

printf '\n=== done ===\n'
