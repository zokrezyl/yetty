#!/bin/bash
# ymsoffice — Word document showcase. Runs ycat on the docx asset in
# demo/assets/ymsoffice/; ycat's msoffice handler parses the ZIP+XML
# container in-process (no external converter) and lays out headings,
# styled runs, nested lists, a table with a spanning cell, an image
# placeholder and alignment as ydraw SDF + MSDF that scrolls with the
# host yetty terminal.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ymsoffice/docx.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/ymsoffice"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ymsoffice: docx ===\n\n'
p

echo '$ ycat demo/assets/ymsoffice/report.docx           # Word: headings, lists, table'
"$YCAT" -c docx "$ASSETS/report.docx"

printf '\n=== done ===\n'
