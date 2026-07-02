#!/bin/bash
# ymesh interactive viewer — orbit / pan / zoom a single mesh.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ymesh/interactive.sh
#
# Or with a specific asset:
#   YMESH_ASSET=demo/assets/ymesh/Avocado.glb \
#       ./build-desktop-ytrace-release/yetty -e demo/scripts/ymesh/interactive.sh
#
# Controls (mirrored in `ymesh --help`):
#   left-drag    orbit
#   right-drag   pan
#   wheel        zoom
#   arrow keys   step orbit
#   W            toggle wireframe / solid
#   F            frame all (reset)
#   R            reset rotation
#   Q / ESC      quit

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YMESH="${YMESH:-$ROOT/build-desktop-ytrace-release/tools/ymesh/ymesh}"
ASSET="${YMESH_ASSET:-$ROOT/demo/assets/ymesh/Duck.glb}"

if [ ! -x "$YMESH" ]; then
    YMESH="$(command -v "${YMESH##*/}" 2>/dev/null || true)"
fi
if [ -z "$YMESH" ] || [ ! -x "$YMESH" ]; then
    echo "ymesh binary not found in build dir or on \$PATH — set YMESH=path/to/ymesh" >&2
    exit 1
fi
if [ ! -f "$ASSET" ]; then
    echo "asset not found in build dir or on \$PATH — set YMESH_ASSET=path/to/file.glb" >&2
    exit 1
fi

printf '=== ymesh interactive viewer ===\n'
printf 'left-drag = orbit  |  right-drag = pan  |  wheel = zoom\n'
printf 'W = wireframe   F = frame   R = reset   Q/ESC = quit\n\n'

exec "$YMESH" -i -w 700 -H 500 "$ASSET"
