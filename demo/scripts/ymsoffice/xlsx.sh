#!/bin/bash
# ymsoffice — Excel workbook showcase. Runs ycat on the xlsx asset in
# demo/assets/ymsoffice/; ycat's msoffice handler resolves shared strings
# and formulas' cached values in-process and draws each sheet as a bordered
# grid with A/1 headers and right-aligned numbers, scrolling with the host
# yetty terminal.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ymsoffice/xlsx.sh

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

printf '=== ymsoffice: xlsx ===\n\n'
p

echo '$ ycat demo/assets/ymsoffice/budget.xlsx           # Excel: grid, formulas, two sheets'
"$YCAT" -c xlsx "$ASSETS/budget.xlsx"

printf '\n=== done ===\n'
