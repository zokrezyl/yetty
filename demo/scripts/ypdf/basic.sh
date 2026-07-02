#!/bin/bash
# ypdf — basic showcase. Runs ycat on the PDF asset in demo/assets/ypdf/; ycat's
# pdf handler parses the page content (text + vector operators, real embedded
# fonts) into ydraw SDF + MSDF primitives that scroll into the host yetty
# terminal's ydraw layer. The sample is a single short page so it fits on screen.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ypdf/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/ypdf"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ypdf basics ===\n\n'
p

echo '$ ycat demo/assets/ypdf/sample.pdf                 # short vector PDF page'
"$YCAT" -c pdf "$ASSETS/sample.pdf"

printf '\n=== done ===\n'
