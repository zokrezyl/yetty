#!/bin/bash
# ybrowser (one-shot) — flexbox: render demo/assets/ybrowser/flexbox.html as one
# ydraw OSC envelope scrolling into the terminal.
#
# Grow ratios (flex: 1/2/1), the iterative shrink pass with a min-width
# floor, wrap with per-line free-space distribution, justify-content
# variants, `order`, row-reverse, and per-item align-self.
#
# Entirely offline — the page and its assets are committed in the repo.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ybrowser/once/flexbox.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../.." && pwd)"
YBROWSER="${YBROWSER:-$ROOT/build-desktop-ytrace-release/tools/ybrowser/ybrowser}"
PAGE="$ROOT/demo/assets/ybrowser/flexbox.html"

if [ ! -x "$YBROWSER" ]; then
    YBROWSER="$(command -v "${YBROWSER##*/}" 2>/dev/null || true)"
fi
if [ -z "$YBROWSER" ] || [ ! -x "$YBROWSER" ]; then
    echo "ybrowser binary not found in build dir or on \$PATH — set YBROWSER=path/to/ybrowser" >&2
    exit 1
fi

printf '=== ybrowser — flexbox ===\n\n'
echo '$ ybrowser --once demo/assets/ybrowser/flexbox.html'
# --osc: under `yetty -e` stdout is a pipe, not a TTY — force the envelope.
exec "$YBROWSER" --once --osc "$PAGE"
