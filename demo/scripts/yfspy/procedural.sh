#!/bin/bash
# yfspy tool — procedural texture fields.
#
# Demoscene-style plasma, nested-loop Voronoi cellular noise, and a polar-fold
# kaleidoscope mandala. plasma and kaleidoscope animate via `time`. The compile
# step needs `uv` on PATH.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yfspy/procedural.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YFSPY="${YFSPY:-$ROOT/build-desktop-ytrace-release/tools/yfspy/yfspy}"
ASSETS="$ROOT/demo/assets/yfspy"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YFSPY" ]; then
    echo "yfspy binary not found at $YFSPY" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yfspy — procedural fields ===\n\n'
p

printf '(1) plasma.py — animated sine-field plasma:\n'
"$YFSPY" "$ASSETS/plasma.py" -w 480 -H 320 \
    --xrange=-3..3 --yrange=-2..2 --no-grid --no-axes
p

printf '\n(2) voronoi.py — cellular noise (nested 3x3 cell loops):\n'
"$YFSPY" "$ASSETS/voronoi.py" -w 480 -H 360 \
    --xrange=0..6 --yrange=0..6 --no-grid --no-axes
p

printf '\n(3) kaleidoscope.py — rotating six-fold mandala:\n'
"$YFSPY" "$ASSETS/kaleidoscope.py" -w 420 -H 420 \
    --xrange=-2..2 --yrange=-2..2 --no-grid --no-axes

printf '\n=== done ===\n'
