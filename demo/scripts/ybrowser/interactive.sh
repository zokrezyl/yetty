#!/bin/bash
# ybrowser (interactive) — a Chrome-like browser: tabbar, back / forward /
# reload, an address bar, and a scrollable rendered page. The lexbor engine
# keeps DOM, JS, timers and click dispatch alive across frames, so links and
# forms actually work.
#
# Loads a committed local page by default, so it comes up OFFLINE. Pass a URL
# (or set YBROWSER_URL) to browse the live web.
#
#   demo/scripts/ybrowser/interactive.sh
#   YBROWSER_URL=https://example.com demo/scripts/ybrowser/interactive.sh
#
# Controls: click links / type a URL in the address bar / scroll / Ctrl-C quits.
#
# Mode is chosen by ybrowser itself from TERM_PROGRAM: run inside yetty
# (TERM_PROGRAM=yetty) → renders in-terminal as a client; run anywhere else
# (TERM_PROGRAM unset / not yetty) → opens its own GPU window with full window
# chrome. We do NOT override that here.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YBROWSER="${YBROWSER:-$ROOT/build-desktop-ytrace-release/tools/ybrowser/ybrowser}"
TARGET="${YBROWSER_URL:-$ROOT/demo/assets/web/welcome.html}"

if [ ! -x "$YBROWSER" ]; then
    YBROWSER="$(command -v "${YBROWSER##*/}" 2>/dev/null || true)"
fi
if [ -z "$YBROWSER" ] || [ ! -x "$YBROWSER" ]; then
    echo "ybrowser binary not found in build dir or on \$PATH — set YBROWSER=path/to/ybrowser" >&2
    exit 1
fi

printf '=== ybrowser — interactive browser ===\n'
printf 'click links | type a URL in the address bar | scroll | Ctrl-C to quit\n\n'

exec "$YBROWSER" --interactive "$TARGET"
