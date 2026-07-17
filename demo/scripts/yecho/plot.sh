#!/bin/bash
# yecho yplot demo — function plots emitted directly into the ydraw buffer.
#
# yecho parses {plot; w=...; h=...; xrange=lo..hi; yrange=lo..hi:
#                f=expr; g=expr; @f.color=#hex}
# and emits a yplot composite (yfsvm-compiled bytecode + uniforms)
# in the same DCS 600001 ydraw envelope used by ycat / ymarkdown / ypdf.
#
# Run inside yetty:  ./build-desktop-ytrace-release/yetty -e demo/scripts/yecho/plot.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YECHO="${YECHO:-$ROOT/build-desktop-ytrace-release/tools/yecho/yecho}"
PAUSE="${DEMO_PAUSE:-3}"

if [ ! -x "$YECHO" ]; then
    YECHO="$(command -v "${YECHO##*/}" 2>/dev/null || true)"
fi
if [ -z "$YECHO" ] || [ ! -x "$YECHO" ]; then
    echo "yecho binary not found in build dir or on \$PATH — set YECHO=path/to/yecho" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== yecho yplot showcase ===\n\n'
p

# Single function — defaults for size/range.
"$YECHO" '{plot: f=sin(x)}'
"$YECHO" '{color=#888888: y = sin(x)  (default range, default colors)}'
p

# Two functions sharing one plot, custom colors.
"$YECHO" '{plot; w=480; h=220:
    f=sin(x);
    g=cos(x);
    @f.color=#FF6B6B;
    @g.color=#4ECDC4
}'
"$YECHO" '{color=#888888: sin and cos overlaid}'
p

# Quadratic vs line — wider Y range, no grid.
"$YECHO" '{plot; w=480; h=220; xrange=-3..3; yrange=-2..10; nogrid:
    parabola=x*x;
    line=2*x+1;
    @parabola.color=#FFE66D;
    @line.color=#AA96DA
}'
"$YECHO" '{color=#888888: y = x^2 vs y = 2x + 1   (wider Y range, no grid)}'
p

# Polynomial — narrower X.
"$YECHO" '{plot; w=480; h=220; xrange=-2..2; yrange=-2..2:
    cubic=x*x*x - x;
    @cubic.color=#95E1D3
}'
"$YECHO" '{color=#888888: y = x^3 - x}'
p

# Headline + plot, all yecho.
"$YECHO" '@equalizer  {color=#FCBF49; style=bold: Audio spectrum}'
"$YECHO" '{plot; w=520; h=180; xrange=0..6.28; yrange=-1..1; nolabels:
    a=sin(x);
    b=sin(2*x)/2;
    c=sin(3*x)/3;
    @a.color=#FF6B6B;
    @b.color=#4ECDC4;
    @c.color=#FFE66D
}'

printf '\n=== done — holding open ===\n'
sleep "${DEMO_HOLD:-600}"
