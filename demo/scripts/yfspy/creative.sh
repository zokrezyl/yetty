#!/bin/bash
# yfspy tool — creative procedural / iterative fields.
#
# These shaders lean on yfsvm control flow (escape/Newton loops), the
# procedural macros (fbm2, domain warp, dist2), and the `time` input for
# animation. All read x and y, so yetty renders them as colormapped heatmap
# fields. The animated ones (julia_animated, ripples) keep ticking. The
# compile step needs `uv` on PATH.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yfspy/creative.sh

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

printf '=== yfspy — creative fields ===\n\n'
p

# (1) Animated Julia set — the constant c orbits a circle with time, so the
# fractal morphs frame to frame.
printf '(1) julia_animated.py — time-morphing Julia set:\n'
"$YFSPY" "$ASSETS/julia_animated.py" -w 480 -H 360 \
    --xrange=-1.6..1.6 --yrange=-1.2..1.2 --no-grid --no-axes
p

# (2) Newton fractal for z^3 - 1 — basins of the three roots, coloured by the
# final angle into three interleaved petals.
printf '\n(2) newton.py — Newton fractal (three root basins):\n'
"$YFSPY" "$ASSETS/newton.py" -w 480 -H 360 \
    --xrange=-1.5..1.5 --yrange=-1.5..1.5 --no-grid --no-axes
p

# (3) Domain-warped fractal noise — marble / clouds.
printf '\n(3) marble.py — domain-warped fbm texture:\n'
"$YFSPY" "$ASSETS/marble.py" -w 480 -H 360 \
    --xrange=-2..2 --yrange=-2..2 --no-grid --no-axes
p

# (4) Animated interference of three circular wave sources.
printf '\n(4) ripples.py — animated wave interference:\n'
"$YFSPY" "$ASSETS/ripples.py" -w 480 -H 360 \
    --xrange=-2.5..2.5 --yrange=-2..2 --no-grid --no-axes

printf '\n=== done ===\n'
