#!/bin/bash
# ybrowser (one-shot) — CSS basics: render demo/assets/ybrowser/css.html as one
# ydraw OSC envelope scrolling into the terminal.
#
# Box model (content-box vs border-box), brand color swatches, floats with
# text wrap, position:relative/absolute (corner pin, inset:0 overlay,
# static position), and CSS custom properties — including a media-query-
# gated width and an element-scoped --var override.
#
# Entirely offline — the page and its assets are committed in the repo.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ybrowser/once/css.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../.." && pwd)"
YBROWSER="${YBROWSER:-$ROOT/build-desktop-ytrace-release/tools/ybrowser/ybrowser}"
PAGE="$ROOT/demo/assets/ybrowser/css.html"

if [ ! -x "$YBROWSER" ]; then
    YBROWSER="$(command -v "${YBROWSER##*/}" 2>/dev/null || true)"
fi
if [ -z "$YBROWSER" ] || [ ! -x "$YBROWSER" ]; then
    echo "ybrowser binary not found in build dir or on \$PATH — set YBROWSER=path/to/ybrowser" >&2
    exit 1
fi

printf '=== ybrowser — CSS basics ===\n\n'
echo '$ ybrowser --once demo/assets/ybrowser/css.html'
# --osc: under `yetty -e` stdout is a pipe, not a TTY — force the envelope.
exec "$YBROWSER" --once --osc "$PAGE"
