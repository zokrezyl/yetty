#!/bin/bash
# ymsoffice — PowerPoint deck showcase. Runs ycat on the pptx asset in
# demo/assets/ymsoffice/; ycat's msoffice handler reads the slide shape
# trees in-process and draws one scaled slide panel per slide — title text,
# brand-colored boxes, connector, ellipse and a picture placeholder —
# scrolling with the host yetty terminal.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ymsoffice/pptx.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/ymsoffice"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ymsoffice: pptx ===\n\n'
p

echo '$ ycat demo/assets/ymsoffice/pitch.pptx            # PowerPoint: three slides'
"$YCAT" -c pptx "$ASSETS/pitch.pptx"

printf '\n=== done ===\n'
