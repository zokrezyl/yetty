#!/bin/bash
# ybrowser (one-shot) — SVG: render demo/assets/ybrowser/svg.html as one
# ydraw OSC envelope scrolling into the terminal.
#
# Inline <svg> subtrees, <img src=*.svg> files and a URL-encoded data-URI —
# every scene renders through ysvg and MERGES into the page's drawable
# list as vectors (SDF segments/boxes/circles); nothing is rasterized.
#
# Entirely offline — the page and its assets are committed in the repo.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ybrowser/once/svg.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../.." && pwd)"
YBROWSER="${YBROWSER:-$ROOT/build-desktop-ytrace-release/tools/ybrowser/ybrowser}"
PAGE="$ROOT/demo/assets/ybrowser/svg.html"

if [ ! -x "$YBROWSER" ]; then
    YBROWSER="$(command -v "${YBROWSER##*/}" 2>/dev/null || true)"
fi
if [ -z "$YBROWSER" ] || [ ! -x "$YBROWSER" ]; then
    echo "ybrowser binary not found in build dir or on \$PATH — set YBROWSER=path/to/ybrowser" >&2
    exit 1
fi

printf '=== ybrowser — SVG ===\n\n'
echo '$ ybrowser --once demo/assets/ybrowser/svg.html'
# --osc: under `yetty -e` stdout is a pipe, not a TTY — force the envelope.
exec "$YBROWSER" --once --osc "$PAGE"
