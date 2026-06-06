#!/bin/bash
# yless demo — page a syntax-highlighted source file over the whole pane.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yless/code.sh
#
# Scroll with j/k, Space/b, g/G, arrows; quit with q (clears the surface).

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YLESS="${YLESS:-$ROOT/build-desktop-ytrace-release/tools/yless/yless}"

if [ ! -x "$YLESS" ]; then
    echo "yless binary not found at $YLESS — set YLESS=path/to/yless" >&2
    exit 1
fi

# Dogfood: page yless's own source over the full pane.
exec "$YLESS" "$ROOT/tools/yless/main.c"
