#!/bin/bash
# Diagnostic demo: same as demo.sh but NO `clear` calls. If this renders
# glyphs but demo.sh doesn't, the bug is the `clear` ESC sequence wiping
# something we depend on.
DIR="$(cd "$(dirname "$0")" && pwd)"
FILES="$DIR/files"
DEMO_PAUSE=${DEMO_PAUSE:-3}

echo "TITLE"
[ "$DEMO_PAUSE" = 0 ] || sleep "$DEMO_PAUSE"

echo "OVERVIEW:"
cat "$FILES/glyphs.txt"
[ "$DEMO_PAUSE" = 0 ] || sleep "$DEMO_PAUSE"

echo
echo "PLASMA TILE:"
bash "$DIR/tile.sh" plasma 4 30
[ "$DEMO_PAUSE" = 0 ] || sleep "$DEMO_PAUSE"

echo
echo "BIOMINE FRACTAL:"
cat "$FILES/fractals/biomine.txt"
[ "$DEMO_PAUSE" = 0 ] || sleep "$DEMO_PAUSE"

sleep "${DEMO_HOLD:-600}"
