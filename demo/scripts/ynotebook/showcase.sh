#!/bin/bash
# ynotebook — render a real Jupyter .ipynb notebook inline. ynb-cat drives the
# yetty_ynotebook model: it parses the notebook, walks every cell, and ships
# each output's RICHEST MIME representation as a figure — the PNG rose, the SVG
# tiger, an HTML table, a markdown block, JSON — so the notebook renders as
# graphics, not base64 text.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ynotebook/showcase.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YNBCAT="${YNBCAT:-$ROOT/build-desktop-ytrace-release/tools/ynb-cat/ynb-cat}"
NOTEBOOK="$ROOT/demo/assets/ynotebook/showcase.ipynb"

if [ ! -x "$YNBCAT" ]; then
    YNBCAT="$(command -v ynb-cat 2>/dev/null || true)"
fi
if [ -z "$YNBCAT" ] || [ ! -x "$YNBCAT" ]; then
    echo "ynb-cat binary not found in build dir or on \$PATH — set YNBCAT=path/to/ynb-cat" >&2
    exit 1
fi

printf '=== ynotebook: rich MIME outputs rendered inline ===\n'
"$YNBCAT" "$NOTEBOOK"
printf '\n=== done ===\n'
