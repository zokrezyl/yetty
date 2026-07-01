#!/bin/bash
# yshadertoy — basic showcase. Runs ycat on the WGSL assets in
# demo/assets/yshadertoy/; ycat's shadertoy handler ships each `mainImage`
# function as a live yshadertoy figure that animates on yetty's frame timer in
# the ydraw layer (Shadertoy-style iResolution / iTime / iMouse uniforms).
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yshadertoy/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/yshadertoy"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== yshadertoy basics (animated figures) ===\n\n'
p

echo '$ ycat demo/assets/yshadertoy/palette.wgsl         # breathing cosine palette'
"$YCAT" -c shadertoy "$ASSETS/palette.wgsl"
p

echo
echo '$ ycat demo/assets/yshadertoy/swirl.wgsl            # rotating polar swirl'
"$YCAT" -c shadertoy "$ASSETS/swirl.wgsl"
p

echo
echo '$ ycat demo/assets/yshadertoy/plasma.wgsl           # classic sine plasma'
"$YCAT" -c shadertoy "$ASSETS/plasma.wgsl"

printf '\n=== done ===\n'
echo "(these keep animating in place — they are live figures, not stills)"
