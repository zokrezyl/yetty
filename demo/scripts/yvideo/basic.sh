#!/bin/bash
# yvideo — basic showcase. Streams short H.264 Annex-B clips as yvideo OSC
# envelopes; the host yetty terminal decodes and plays them in the ydraw layer.
# The committed clips are tiny synthetic patterns (test pattern + SMPTE bars).
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yvideo/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YVIDEO="${YVIDEO:-$ROOT/build-desktop-ytrace-release/tools/yvideo/yvideo}"
ASSETS="$ROOT/demo/assets/yvideo"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YVIDEO" ]; then
    echo "yvideo binary not found at $YVIDEO — set YVIDEO=path/to/yvideo" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yvideo basics ===\n\n'
p

echo '$ yvideo demo/assets/yvideo/testsrc.h264          # animated test pattern, autoplay+loop'
"$YVIDEO" "$ASSETS/testsrc.h264"
p

echo
echo '$ yvideo --no-loop --bounds-w=480 demo/assets/yvideo/smpte.h264  # SMPTE bars, play once'
"$YVIDEO" --no-loop --bounds-w=480 "$ASSETS/smpte.h264"

printf '\n=== done ===\n'
