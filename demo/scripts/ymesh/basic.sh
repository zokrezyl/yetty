#!/bin/bash
# ymesh tool — basic showcase. Runs the standalone ymesh binary on a few
# .glb files from demo/assets/ymesh/ and lets the OSC envelopes scroll into
# the ydraw layer of the host yetty terminal.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ymesh/basic.sh
#
# Or from anywhere (launches yetty, runs the script, holds open):
#   yetty -e demo/scripts/ymesh/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YMESH="${YMESH:-$ROOT/build-desktop-ytrace-release/tools/ymesh/ymesh}"
ASSETS="$ROOT/demo/assets/ymesh"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YMESH" ]; then
    echo "ymesh binary not found at $YMESH — set YMESH=path/to/ymesh" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ymesh tool basics ===\n\n'
p

# Box — the canonical glTF smoke-test cube. 24 vertices / 36 indices.
echo '$ ymesh demo/assets/ymesh/Box.glb'
"$YMESH" "$ASSETS/Box.glb"
p

# Duck — classic test model with curvature. Lambert shading shows it well.
echo
echo '$ ymesh demo/assets/ymesh/Duck.glb'
"$YMESH" "$ASSETS/Duck.glb"
p

# Avocado — organic surface, good for showing normals over a curved mesh.
# Larger display so the geometry is visible at a comfortable size.
echo
echo '$ ymesh -w 480 -H 480 demo/assets/ymesh/Avocado.glb'
"$YMESH" -w 480 -H 480 "$ASSETS/Avocado.glb"
p

# Side-by-side small renders.
echo
echo 'thumbnails:'
"$YMESH" -w 200 -H 200 "$ASSETS/Box.glb"
"$YMESH" -w 200 -H 200 "$ASSETS/Duck.glb"
"$YMESH" -w 200 -H 200 "$ASSETS/Avocado.glb"

printf '\n=== done — holding open ===\n'
sleep 600
