#!/bin/bash
# ymarkdown — basic showcase. Runs ycat on the Markdown assets in
# demo/assets/ymarkdown/; ycat's markdown handler lays out headings, lists,
# code blocks, tables and inline styles as ydraw SDF + MSDF that scroll with
# the host yetty terminal.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ymarkdown/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/ymarkdown"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ymarkdown basics ===\n\n'
p

echo '$ ycat demo/assets/ymarkdown/overview.md           # headings, lists, code, table'
"$YCAT" -c markdown "$ASSETS/overview.md"
p

echo
echo '$ ycat demo/assets/ymarkdown/cheatsheet.md          # heading scale + rules + quotes'
"$YCAT" -c markdown "$ASSETS/cheatsheet.md"

printf '\n=== done ===\n'
