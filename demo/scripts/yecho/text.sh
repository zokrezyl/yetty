#!/bin/bash
# yecho text demo — colors, glyphs, styled blocks.
#
# Run inside yetty:  ./build-desktop-ytrace-release/yetty -e demo/scripts/yecho/text.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YECHO="${YECHO:-$ROOT/build-desktop-ytrace-release/tools/yecho/yecho}"
PAUSE="${DEMO_PAUSE:-2}"

if [ ! -x "$YECHO" ]; then
    echo "yecho binary not found at $YECHO — set YECHO=path/to/yecho" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yecho text showcase ===\n\n'
p

# Plain colored runs.
"$YECHO" '{color=#FF6B6B: red text} and {color=#4ECDC4: cyan text} on a line.'
p

"$YECHO" '{bg=#3D3D3D: highlighted run} continues into normal text.'
p

# Inline glyphs alongside text.
"$YECHO" 'Loading @spinner please hold @hourglass'
p

"$YECHO" 'Status indicators: @heart healthy  @rocket launching  @gem ready'
p

# Combined — colors + glyphs in one line.
"$YECHO" '{color=#FFE66D: warning} @signal-bars connection unstable'
p

# Multi-line content.
"$YECHO" "$(cat <<'EOF'
yecho line one
yecho line two with @sparkle inline
{color=#AA96DA: line three styled}
EOF
)"

printf '\n=== done — holding open ===\n'
sleep 600
