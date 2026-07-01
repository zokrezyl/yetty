#!/bin/bash
# yflame — basic showcase. Renders folded-stack samples (perf/FlameGraph format)
# as flame-graph OSC envelopes in the host yetty ydraw layer. Uses the committed
# sample profile in demo/assets/yflame/ and also shows the stdin pipe path.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yflame/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YFLAME="${YFLAME:-$ROOT/build-desktop-ytrace-release/tools/yflame/yflame}"
ASSETS="$ROOT/demo/assets/yflame"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YFLAME" ]; then
    YFLAME="$(command -v "${YFLAME##*/}" 2>/dev/null || true)"
fi
if [ -z "$YFLAME" ] || [ ! -x "$YFLAME" ]; then
    echo "yflame binary not found in build dir or on \$PATH — set YFLAME=path/to/yflame" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== yflame basics ===\n\n'
p

echo '$ yflame demo/assets/yflame/profile.folded         # classic flame (root at bottom)'
"$YFLAME" "$ASSETS/profile.folded"
p

echo
echo '$ yflame --icicle demo/assets/yflame/profile.folded  # icicle (root at top)'
"$YFLAME" --icicle "$ASSETS/profile.folded"
p

echo
echo '$ yflame -w 900 --no-labels demo/assets/yflame/profile.folded  # wide, no labels'
"$YFLAME" -w 900 --no-labels "$ASSETS/profile.folded"
p

# Folded stacks straight off a pipe (the usual `perf script | stackcollapse | yflame`).
# yflame reads stdin when given no file argument.
echo
echo '$ printf "...collapsed stacks..." | yflame'
printf 'a;b;c 10\na;b;d 20\na;e 30\n' | "$YFLAME"

printf '\n=== done ===\n'
echo "(tip: yflame -I demo/assets/yflame/profile.folded for the interactive, zoomable figure)"
