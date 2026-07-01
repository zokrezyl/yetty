#!/bin/bash
# yfspy tool — signal / waveform line plots.
#
# Line plots that show off animation (the `time` input) and in-shader control
# flow (a Fourier sum with an alternating sign) alongside the procedural-noise
# macros. The compile step needs `uv` on PATH.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yfspy/signals.sh

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

printf '=== yfspy — signals & waveforms ===\n\n'
p

# (1) A travelling Gaussian wave packet — carrier sine moving with time under
# a fixed envelope. Animated.
printf '(1) pulse.py — travelling Gaussian wave packet (animated):\n'
"$YFSPY" "$ASSETS/pulse.py" -w 600 -H 240 --xrange=-6.28..6.28 --yrange=-1..1
p

# (2) Sawtooth via its Fourier series, summed in a while-loop with an
# alternating sign.
printf '\n(2) sawtooth.py — Fourier sawtooth via an in-shader loop:\n'
"$YFSPY" "$ASSETS/sawtooth.py" -w 600 -H 240 --xrange=-6.28..6.28 --yrange=-1.2..1.2
p

# (3) A ridged-noise terrain silhouette sampled along y = 0.
printf '\n(3) terrain.py — ridged-noise mountain profile:\n'
"$YFSPY" "$ASSETS/terrain.py" -w 600 -H 240 --xrange=-4..4 --yrange=-1..1.2

printf '\n=== done ===\n'
