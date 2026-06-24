#!/bin/bash
# ydiagram — basic showcase. Runs the standalone ydiagram tool on a few Mermaid
# diagram assets in demo/assets/ydiagram/ and lets the OSC YDRAW_BIN envelopes
# scroll into the ydraw layer of the host yetty terminal. Box sizing uses real
# MSDF glyph widths.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ydiagram/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YDIAGRAM="${YDIAGRAM:-$ROOT/build-desktop-ytrace-release/tools/ydiagram/ydiagram}"
ASSETS="$ROOT/demo/assets/ydiagram"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YDIAGRAM" ]; then
    echo "ydiagram binary not found at $YDIAGRAM — set YDIAGRAM=path/to/ydiagram" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ydiagram basics ===\n\n'
p

echo '$ ydiagram demo/assets/ydiagram/directions.mmd   # flowchart'
"$YDIAGRAM" "$ASSETS/directions.mmd"
p

echo
echo '$ ydiagram demo/assets/ydiagram/state-machine.mmd  # state diagram'
"$YDIAGRAM" "$ASSETS/state-machine.mmd"
p

echo
echo '$ ydiagram demo/assets/ydiagram/sequence-diagram.mmd  # sequence'
"$YDIAGRAM" "$ASSETS/sequence-diagram.mmd"
p

echo
echo '$ ydiagram demo/assets/ydiagram/er-diagram.mmd     # entity-relationship'
"$YDIAGRAM" "$ASSETS/er-diagram.mmd"
p

# Piped through stdin with the explicit `-` argument.
echo
echo '$ cat demo/assets/ydiagram/cycle.mmd | ydiagram -  # flowchart from stdin'
cat "$ASSETS/cycle.mmd" | "$YDIAGRAM" -

printf '\n=== done ===\n'
