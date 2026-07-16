#!/bin/bash
# Runs all ten yshadertoy demos against a single yetty and collects one GPU
# screenshot per shader (captured with yetty's own `screenshot` RPC, not an
# X11 grab). Each shader is fetched from shadertoy.com with no API key,
# converted GLSL -> SPIR-V -> WGSL, and drawn into yetty by ycat.
#
# Prereq: a yetty listening on the RPC port (recommended), started in a
# different terminal than this one:
#   ./build-desktop-ytrace-release/yetty --rpc-port=9999 -e bash
# then:
#   ./demo/scripts/yshadertoy/run-all.sh
#
# If nothing is listening, the harness launches a throwaway yetty, runs the
# ten shaders, and shuts it down again. The tint CLI must be reachable
# (export TINT=/path/to/tint from a dawn-exotic release) for the WGSL step.
#
# Screenshots land in tmp/yshadertoy-demo/shots/<id>.png.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_common.sh"

# id  title (all confirmed single Image pass, no texture channels)
DEMOS=(
    "XsXXDn|Creation by Silexars"
    "Ms2SD1|Seascape"
    "XlfGRj|Star Nest"
    "Xds3zN|Raymarching - Primitives"
    "3l23Rh|Protean Clouds"
    "4sX3Rn|Menger Sponge"
    "ttKGDt|Phantom Star"
    "XtGGRt|Auroras"
    "4tlSzl|Combustible Voronoi"
    "lsl3RH|Warping - procedural 2"
)

st_ensure_yetty
for entry in "${DEMOS[@]}"; do
    id="${entry%%|*}"
    title="${entry#*|}"
    st_demo "$id" "$title"
done

echo "==> all done — screenshots in $SHOTDIR"
ls -1 "$SHOTDIR" 2>/dev/null
