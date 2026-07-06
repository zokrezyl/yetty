#!/bin/bash
# ybrowser (one-shot) — CSS grid: render demo/assets/ybrowser/grid.html as one
# ydraw OSC envelope scrolling into the terminal.
#
# Track templates mixing px / 1fr / 2fr, a sidebar layout stretching to the
# row height, grid-column: span 2 placement, and a 36-cell auto-flow grid.
#
# Entirely offline — the page and its assets are committed in the repo.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ybrowser/once/grid.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../.." && pwd)"
YBROWSER="${YBROWSER:-$ROOT/build-desktop-ytrace-release/tools/ybrowser/ybrowser}"
PAGE="$ROOT/demo/assets/ybrowser/grid.html"

if [ ! -x "$YBROWSER" ]; then
    YBROWSER="$(command -v "${YBROWSER##*/}" 2>/dev/null || true)"
fi
if [ -z "$YBROWSER" ] || [ ! -x "$YBROWSER" ]; then
    echo "ybrowser binary not found in build dir or on \$PATH — set YBROWSER=path/to/ybrowser" >&2
    exit 1
fi

printf '=== ybrowser — CSS grid ===\n\n'
echo '$ ybrowser --once demo/assets/ybrowser/grid.html'
# --osc: under `yetty -e` stdout is a pipe, not a TTY — force the envelope.
exec "$YBROWSER" --once --osc "$PAGE"
