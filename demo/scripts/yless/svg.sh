#!/bin/bash
# yless demo — an SVG drawing in a 48x24-cell box anchored at the top-left.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yless/svg.sh
#
# Quit with q (clears the surface).

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YLESS="${YLESS:-$ROOT/build-desktop-ytrace-release/tools/yless/yless}"

if [ ! -x "$YLESS" ]; then
    YLESS="$(command -v "${YLESS##*/}" 2>/dev/null || true)"
fi
if [ -z "$YLESS" ] || [ ! -x "$YLESS" ]; then
    echo "yless binary not found in build dir or on \$PATH — set YLESS=path/to/yless" >&2
    exit 1
fi

exec "$YLESS" -x 0 -y 0 -w 48 -H 24 "$ROOT/demo/assets/svg/tiger.svg"
