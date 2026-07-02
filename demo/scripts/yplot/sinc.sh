#!/bin/bash
# yplot tool — the sinc function, sin(x)/x.
#
# Showcases the built-in `sinc` opcode, which evaluates sin(x)/x with the
# removable singularity at the origin filled in (sinc(0) = 1) so there is no
# NaN spike where bare `sin(x)/x` divides by zero.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/sinc.sh

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

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== yplot — sinc, the cardinal sine ===\n\n'
p

# (1) The singularity, fixed. `sinc(x)` rides cleanly through a peak of 1 at
# the origin; bare `sin(x)/x` is 0/0 there and leaves a hole. Same curve
# everywhere else — the opcode just patches x=0.
printf '(1) sinc(x) vs raw sin(x)/x at the origin:\n'
"$YPLOT" -w 600 -H 240 --xrange=-15.7..15.7 --yrange=-0.3..1.1 \
    'raw=sin(x)/x' \
    'fixed=sinc(x)' \
    '@raw.color=#FF6B6B' \
    '@fixed.color=#74C5A5'
p

# (2) Single-slit diffraction. The far-field intensity of light through a
# slit is sinc² — a bright central fringe with rapidly dimming side lobes.
printf '\n(2) Single-slit diffraction intensity (sinc squared):\n'
"$YPLOT" -w 600 -H 240 --xrange=-12.56..12.56 --yrange=-0.05..1.05 \
    'amplitude=sinc(x)' \
    'intensity=sinc(x)*sinc(x)' \
    '@amplitude.color=#364A47' \
    '@intensity.color=#6BA892'
p

# (3) Fourier duality. A wider aperture (faster argument) produces a
# narrower sinc — wide in one domain is narrow in the other.
printf '\n(3) Aperture width vs lobe width:\n'
"$YPLOT" -w 600 -H 240 --xrange=-9.42..9.42 --yrange=-0.3..1.1 \
    'narrow=sinc(x)' \
    'wider=sinc(x*2)' \
    'widest=sinc(x*4)' \
    '@narrow.color=#5A8979' \
    '@wider.color=#6BA892' \
    '@widest.color=#74C5A5'
p

# (4) A Lanczos resampling kernel: a sinc multiplied by a wider sinc window
# (a=3). This is the weighting function high-quality image scalers convolve
# with.
printf '\n(4) Lanczos-3 resampling kernel:\n'
"$YPLOT" -w 600 -H 240 --xrange=-9.42..9.42 --yrange=-0.3..1.1 \
    'window=sinc(x/3)' \
    'lanczos=sinc(x)*sinc(x/3)' \
    '@window.color=#364A47' \
    '@lanczos.color=#74C5A5'

printf '\n=== done — holding open ===\n'
