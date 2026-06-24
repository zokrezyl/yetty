#!/bin/bash
# ymap — basic showcase. Renders slippy maps into the host yetty ydraw layer
# through the standalone ymap tool. Each view fetches tiles over HTTPS, so this
# demo REQUIRES network access (and the tile providers being reachable).
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ymap/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YMAP="${YMAP:-$ROOT/build-desktop-ytrace-release/tools/ymap/ymap}"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YMAP" ]; then
    echo "ymap binary not found at $YMAP — set YMAP=path/to/ymap" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ymap basics (needs network) ===\n\n'
p

echo '$ ymap --list-providers'
"$YMAP" --list-providers || true
p

echo
echo '$ ymap --lat 51.5074 --lon -0.1278 -z 12          # London, OSM raster'
"$YMAP" --lat 51.5074 --lon -0.1278 -z 12 || echo "(map fetch failed — offline?)"
p

echo
echo '$ ymap --lat 40.6892 --lon -74.0445 -z 13 -P s2cloudless  # NYC, satellite'
"$YMAP" --lat 40.6892 --lon -74.0445 -z 13 -P s2cloudless || echo "(map fetch failed — offline?)"
p

echo
echo '$ ymap --lat 48.8584 --lon 2.2945 -z 14 -V          # Paris (Eiffel), vector'
"$YMAP" --lat 48.8584 --lon 2.2945 -z 14 -V || echo "(map fetch failed — offline?)"

printf '\n=== done ===\n'
echo "(tip: ymap -i  for an interactive, drag-to-pan, wheel-to-zoom map)"
