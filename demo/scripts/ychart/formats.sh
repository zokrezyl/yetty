#!/bin/bash
# ychart — input format coverage. The same chart engine reads three data
# formats; this walks one of each plus the two detection routes (a `.chart`
# extension vs. a content-sniffed `#ychart` directive / chart key).
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ychart/formats.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/ychart"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ychart input formats ===\n\n'
p

echo '--- CSV (with a #ychart directive line) ---'
echo '$ ycat demo/assets/ychart/revenue.chart'
"$YCAT" "$ASSETS/revenue.chart"
p

echo
echo '--- JSON (top-level "chart" key) ---'
echo '$ ycat demo/assets/ychart/browsers.json'
"$YCAT" "$ASSETS/browsers.json"
p

echo
echo '--- YAML (top-level chart: key) ---'
echo '$ ycat demo/assets/ychart/disk-usage.yaml'
"$YCAT" "$ASSETS/disk-usage.yaml"
p

# Detection routes: the .chart extension is mapped directly, while a plain
# .csv / .json / .yaml is only claimed when its content carries a chart
# marker — so a normal data file is never hijacked.
echo
echo '--- content sniff over stdin (no extension at all) ---'
echo '$ cat demo/assets/ychart/skills.yaml | ycat -'
cat "$ASSETS/skills.yaml" | "$YCAT" -

printf '\n=== done ===\n'
