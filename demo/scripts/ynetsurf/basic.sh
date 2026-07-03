#!/bin/bash
# ynetsurf — the NetSurf 3.11 layout engine, rendering real HTML into the host
# yetty ydraw layer. In one-shot mode (--once) it navigates a URL, waits for
# layout, emits ONE ydraw OSC envelope, and exits — exactly like ycat/yplot,
# so the page scrolls into the terminal alongside text.
#
# The first render uses a committed local page (file://), so this part works
# fully OFFLINE. The live-URL render at the end needs network and degrades
# gracefully when offline.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ynetsurf/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YNETSURF="${YNETSURF:-$ROOT/build-desktop-ytrace-release/tools/ynetsurf/ynetsurf}"
PAGE="$ROOT/demo/assets/web/welcome.html"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YNETSURF" ]; then
    YNETSURF="$(command -v "${YNETSURF##*/}" 2>/dev/null || true)"
fi
if [ -z "$YNETSURF" ] || [ ! -x "$YNETSURF" ]; then
    echo "ynetsurf binary not found in build dir or on \$PATH — set YNETSURF=path/to/ynetsurf" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ynetsurf — HTML rendered by NetSurf, inline in the terminal ===\n\n'
p

# --once forces one-shot even though stdout is a TTY; --osc emits the ydraw
# envelope the host ydraw-layer consumes. Offline: renders the committed page.
echo '$ ynetsurf --once demo/assets/web/welcome.html   # offline, local page'
"$YNETSURF" --once --osc "file://$PAGE" || echo "(render failed)"
p

# A live page — needs network; falls back to a note when offline.
echo
echo '$ ynetsurf --once https://example.com             # live web (needs network)'
"$YNETSURF" --once --osc --wait 6000 https://example.com \
    || echo "(fetch failed — offline?)"

printf '\n=== done ===\n'
echo "(tip: ynetsurf <url>  with no --once opens an interactive, scrollable page)"
