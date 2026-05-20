#!/bin/bash
# yplot tool — domain & viewport language showcase.
#
# Demonstrates the inline domain/viewport syntax inside the expression itself
# (an alternative to the --xrange / --yrange CLI flags):
#   - `x=A..B`           evaluation domain along x
#   - `y=A..B`           evaluation domain along y (only used by 2D modes)
#   - `@view=A..B,C..D`  initial visible rectangle (overrides domain framing)
#
# Numeric bounds accept constants pi / tau / e and unary minus.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/domain-and-view.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YPLOT="${YPLOT:-$ROOT/build-desktop-ytrace-release/tools/yplot/yplot}"
PAUSE="${DEMO_PAUSE:-2}"

if [ ! -x "$YPLOT" ]; then
    echo "yplot binary not found at $YPLOT" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yplot domain & viewport ===\n\n'
p

# (1) Inline domain replaces --xrange. `x=-pi..pi` is parsed inside the
# expression itself.
printf '(1) inline x=-pi..pi:\n'
"$YPLOT" -w 520 -H 160 \
    'x=-pi..pi; sine=sin(x); cosine=cos(x); @sine.color=#FF6B6B; @cosine.color=#4ECDC4'
p

# (2) Inline x and y ranges together. y=-1.5..1.5 sets the vertical bounds
# (equivalent to --yrange).
printf '\n(2) inline x and y ranges:\n'
"$YPLOT" -w 520 -H 160 \
    'x=0..tau; y=-1.2..1.2; ripple=sin(x)+0.3*sin(5*x); @ripple.color=#FCBF49'
p

# (3) @view= overrides the framing without changing the domain — useful for
# zooming into a region without resampling the expression.
printf '\n(3) @view= zoom-in:\n'
"$YPLOT" -w 520 -H 160 \
    'x=-10..10; @view=-pi..pi,-0.5..1.5; signal=sin(x)/x; @signal.color=#74C5A5'
p

# (4) Wide domain with a deliberately tighter viewport. The function is
# evaluated across the full domain, but only the viewport is rendered.
printf '\n(4) wide eval, narrow view:\n'
"$YPLOT" -w 520 -H 160 \
    'x=-10..10; @view=-2..2,-1..1; damped=sin(x)*exp(-abs(x)/3); @damped.color=#AA96DA'

printf '\n=== done — holding open ===\n'
sleep 600
