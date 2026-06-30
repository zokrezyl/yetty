#!/bin/bash
# ycircuit — symbol coverage. Renders the gallery of every component kind
# and the rotation-coverage sheet from demo/assets/ycircuit/.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ycircuit/symbols.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/ycircuit"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ycircuit symbol coverage ===\n\n'
p

echo '$ ycat demo/assets/ycircuit/symbols.circuit'
"$YCAT" "$ASSETS/symbols.circuit"
p

echo
echo '$ ycat demo/assets/ycircuit/rotations.circuit'
"$YCAT" "$ASSETS/rotations.circuit"

printf '\n=== done ===\n'
