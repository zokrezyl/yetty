#!/bin/bash
# ymusic — basic showcase. Runs ycat on the LilyPond (.ly) assets in
# demo/assets/music/; ycat's music handler engraves the notation (staff, clefs,
# noteheads, beams, accidentals) with the Emmentaler font into the ydraw layer.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ymusic/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/music"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ymusic basics ===\n\n'
p

echo '$ ycat demo/assets/music/c-major-scale.ly         # scale up and down'
"$YCAT" "$ASSETS/c-major-scale.ly"
p

echo
echo '$ ycat demo/assets/music/chords.ly                  # stacked chords + accidentals'
"$YCAT" "$ASSETS/chords.ly"
p

echo
echo '$ ycat demo/assets/music/ode-to-joy.ly              # a melody wrapping several systems'
"$YCAT" "$ASSETS/ode-to-joy.ly"

printf '\n=== done ===\n'
