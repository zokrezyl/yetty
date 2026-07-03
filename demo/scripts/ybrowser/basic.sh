#!/bin/bash
# ybrowser (one-shot) — the lexbor DOM/CSS engine rendering HTML into the host
# yetty ydraw layer. With --once it reads HTML from a file (or stdin, or a URL),
# lays it out, emits ONE ydraw OSC envelope, and exits — the page scrolls into
# the terminal like any other ycat-style figure.
#
# This is the OFFLINE, scrolling counterpart to the interactive Chrome-like
# mode (see interactive.sh). The local render needs no network.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ybrowser/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YBROWSER="${YBROWSER:-$ROOT/build-desktop-ytrace-release/tools/ybrowser/ybrowser}"
PAGE="$ROOT/demo/assets/web/welcome.html"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YBROWSER" ]; then
    YBROWSER="$(command -v "${YBROWSER##*/}" 2>/dev/null || true)"
fi
if [ -z "$YBROWSER" ] || [ ! -x "$YBROWSER" ]; then
    echo "ybrowser binary not found in build dir or on \$PATH — set YBROWSER=path/to/ybrowser" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ybrowser — HTML rendered by lexbor, inline in the terminal ===\n\n'
p

# Render a committed local file. --once forces one-shot; --osc forces the OSC
# ydraw envelope explicitly (under `yetty -e` the child's stdout is a pipe, not
# a TTY, so we must not rely on the isatty auto-detect).
echo '$ ybrowser --once demo/assets/web/welcome.html   # offline, local page'
"$YBROWSER" --once --osc "$PAGE" || echo "(render failed)"
p

# The same one-shot path also accepts HTML straight off stdin.
echo
echo '$ echo "<h1>...</h1>" | ybrowser --once             # HTML from stdin'
printf '%s' '<body style="background:#0B1014;color:#74C5A5;font-family:sans-serif;padding:24px">
<h1>Piped straight from stdin</h1>
<p style="color:#E0E5E4">Any HTML fragment becomes a figure — no file needed.</p>
<ul><li>lexbor DOM</li><li>CSS box model</li><li>one OSC envelope out</li></ul>
</body>' | "$YBROWSER" --once --osc - || echo "(render failed)"

printf '\n=== done ===\n'
echo "(tip: ybrowser <url>  with no --once opens the interactive Chrome-like browser)"
