#!/bin/bash
# yecho — text rendered at varying font sizes via the `font-size=N` block
# attribute. Demonstrates that the ypaint scrolling layer accepts arbitrary
# pixel-sized text spans (not just terminal-cell sized).
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yecho/text-sizes.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YECHO="${YECHO:-$ROOT/build-desktop-ytrace-release/tools/yecho/yecho}"
PAUSE="${DEMO_PAUSE:-1}"

if [ ! -x "$YECHO" ]; then
    echo "yecho binary not found at $YECHO" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yecho font-size showcase ===\n\n'
p

# Headline-size text.
"$YECHO" '{font-size=48; color=#FFE66D: Yetty}'
p
"$YECHO" '{font-size=32; color=#4ECDC4: a GPU terminal}'
p
"$YECHO" '{font-size=24; color=#AA96DA: with rich content layers}'
p

# Default size, for contrast.
"$YECHO" '(this line uses the default cell-sized font)'
p

# Color sweeps at the same big size.
"$YECHO" '{font-size=40; color=#FF6B6B: red}  ' \
         '{font-size=40; color=#FFE66D: yellow}  ' \
         '{font-size=40; color=#4ECDC4: cyan}'
p

# Sized text mixed with inline glyphs (glyph stays cell-sized).
"$YECHO" '{font-size=32; color=#95E1D3: Loading} @spinner'
p
"$YECHO" '{font-size=32; color=#FCBF49: Ready} @gem'
p

# A small-print line.
"$YECHO" '{font-size=10; color=#888888: tiny print at 10px}'
p

# Background highlight at custom size.
"$YECHO" '{font-size=28; color=#000000; bg=#FFE66D: BREAKING}'
p

# Multi-line block at one size.
"$YECHO" "$(cat <<'EOF'
{font-size=18; color=#E6E6E6: This block spans
several lines all rendered
at the same custom size.}
EOF
)"

printf '\n=== done — holding open ===\n'
sleep 600
