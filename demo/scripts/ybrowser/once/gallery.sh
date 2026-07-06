#!/bin/bash
# ybrowser (one-shot) — render every demo/assets/ybrowser aspect page in
# sequence, one ydraw OSC envelope each, scrolling into the terminal. The
# sibling scripts (css.sh, flexbox.sh, grid.sh, tables.sh, svg.sh,
# images.sh) run any single page on its own. Each
# page pins one part of the engine: CSS box model + custom properties,
# flexbox (grow/shrink/wrap/order/reverse), grid (tracks/spans/stretch),
# tables, vector SVG (inline / img / data URI), and raster images.
#
# Entirely offline — every page and image is committed in the repo.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ybrowser/once/gallery.sh
#
# DEMO_PAUSE=2 adds a pause between pages; PAGES="grid svg" picks a subset.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../.." && pwd)"
YBROWSER="${YBROWSER:-$ROOT/build-desktop-ytrace-release/tools/ybrowser/ybrowser}"
ASSETS="$ROOT/demo/assets/ybrowser"
PAUSE="${DEMO_PAUSE:-0}"
PAGES="${PAGES:-css flexbox grid tables svg images}"

if [ ! -x "$YBROWSER" ]; then
    YBROWSER="$(command -v "${YBROWSER##*/}" 2>/dev/null || true)"
fi
if [ -z "$YBROWSER" ] || [ ! -x "$YBROWSER" ]; then
    echo "ybrowser binary not found in build dir or on \$PATH — set YBROWSER=path/to/ybrowser" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ybrowser gallery — one engine aspect per page ===\n'
for page in $PAGES; do
    printf '\n$ ybrowser --once demo/assets/ybrowser/%s.html\n' "$page"
    # --osc: under `yetty -e` stdout is a pipe, not a TTY — force the envelope.
    "$YBROWSER" --once --osc "$ASSETS/$page.html" || echo "($page render failed)"
    p
done
printf '\n=== done ===\n'
echo "(tip: demo/scripts/ybrowser/interactive/browser.sh opens the clickable hub)"
