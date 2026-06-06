#!/bin/bash
# yplot tool — radians and degrees conversion.
#
# Showcases the built-in `radians` and `degrees` opcodes (the GLSL/WGSL angle
# converters): radians(d) = d * pi/180, degrees(r) = r * 180/pi. They let an
# expression work in whichever angular unit reads naturally for the axis.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/angle-conversion.sh

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

printf '=== yplot — radians / degrees ===\n\n'
p

# (1) Trig on a degree axis. sin/cos expect radians, so wrapping the domain
# in radians() lets the x-axis run 0..360 in familiar degrees while the
# curves stay correct.
printf '(1) sin & cos over a 0..360 degree axis:\n'
"$YPLOT" -w 600 -H 240 --xrange=0..360 --yrange=-1.2..1.2 \
    'sine=sin(radians(x))' \
    'cosine=cos(radians(x))' \
    '@sine.color=#6BA892' \
    '@cosine.color=#74C5A5'
p

# (2) Slope to angle. degrees(atan(slope)) reads out the inclination of a
# line directly in degrees — flat at 0, approaching +/-90 as the slope runs
# away. The grade-to-angle conversion on a road sign.
printf '\n(2) Slope to angle: degrees(atan(slope)):\n'
"$YPLOT" -w 600 -H 240 --xrange=-10..10 --yrange=-90..90 \
    'angle=degrees(atan(x))' \
    '@angle.color=#FFE66D'
p

# (3) A phase shift expressed in degrees. The second wave is offset by 90
# degrees — passed straight through radians() so the shift is written in the
# same units as the axis.
printf '\n(3) 90-degree phase shift on a degree axis:\n'
"$YPLOT" -w 600 -H 240 --xrange=0..360 --yrange=-1.2..1.2 \
    'reference=sin(radians(x))' \
    'shifted=sin(radians(x - 90))' \
    '@reference.color=#556162' \
    '@shifted.color=#74C5A5'

printf '\n=== done — holding open ===\n'
