#!/bin/bash
# ybrowser (one-shot) — tables: render demo/assets/ybrowser/tables.html as one
# ydraw OSC envelope scrolling into the terminal.
#
# A floated Wikipedia-style infobox with label/value columns and a
# full-width zebra-striped data table with a header row.
#
# Entirely offline — the page and its assets are committed in the repo.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ybrowser/once/tables.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../.." && pwd)"
YBROWSER="${YBROWSER:-$ROOT/build-desktop-ytrace-release/tools/ybrowser/ybrowser}"
PAGE="$ROOT/demo/assets/ybrowser/tables.html"

if [ ! -x "$YBROWSER" ]; then
    YBROWSER="$(command -v "${YBROWSER##*/}" 2>/dev/null || true)"
fi
if [ -z "$YBROWSER" ] || [ ! -x "$YBROWSER" ]; then
    echo "ybrowser binary not found in build dir or on \$PATH — set YBROWSER=path/to/ybrowser" >&2
    exit 1
fi

printf '=== ybrowser — tables ===\n\n'
echo '$ ybrowser --once demo/assets/ybrowser/tables.html'
# --osc: under `yetty -e` stdout is a pipe, not a TTY — force the envelope.
exec "$YBROWSER" --once --osc "$PAGE"
