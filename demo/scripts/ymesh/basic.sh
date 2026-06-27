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
PAUSE="${DEMO_PAUSE:-0.1}"

if [ ! -x "$YMESH" ]; then
    echo "ymesh binary not found at $YMESH — set YMESH=path/to/ymesh" >&2
    exit 1
fi

# This is the one-shot showcase: each invocation emits a single figure that
# anchors on its last line and scrolls into the ydraw layer with the text.
# Force --once so the tool does not enter its interactive viewer (which holds
# the foreground and would block the rest of the script). The interactive
# orbit/zoom viewer has its own demo in interactive.sh.
ymesh() { "$YMESH" --once "$@"; }

p() { sleep "$PAUSE"; }

printf '=== ymesh tool basics ===\n\n'
p

# Box — the canonical glTF smoke-test cube. 24 vertices / 36 indices.
echo '$ ymesh demo/assets/ymesh/Box.glb'
ymesh "$ASSETS/Box.glb"
p

# Duck — classic test model with curvature. Lambert shading shows it well.
echo
echo '$ ymesh demo/assets/ymesh/Duck.glb'
ymesh "$ASSETS/Duck.glb"
p

# Avocado — organic surface, good for showing normals over a curved mesh.
# Larger display so the geometry is visible at a comfortable size.
echo
echo '$ ymesh -w 480 -H 480 demo/assets/ymesh/Avocado.glb'
ymesh -w 480 -H 480 "$ASSETS/Avocado.glb"
p

# Side-by-side small renders.
echo
echo 'thumbnails:'
ymesh -w 200 -H 200 "$ASSETS/Box.glb"
ymesh -w 200 -H 200 "$ASSETS/Duck.glb"
ymesh -w 200 -H 200 "$ASSETS/Avocado.glb"

printf '\n=== done — holding open ===\n'
sleep 600
