#!/bin/bash
# ychart — basic showcase. Runs ycat on a representative subset of the chart
# assets in demo/assets/ychart/ and lets the OSC envelopes scroll into the
# ydraw layer of the host yetty terminal.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ychart/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/ychart"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    echo "ycat binary not found at $YCAT — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ychart basics ===\n\n'
p

echo '$ ycat demo/assets/ychart/revenue.chart        # column'
"$YCAT" "$ASSETS/revenue.chart"
p

echo
echo '$ ycat demo/assets/ychart/languages.chart       # horizontal bar'
"$YCAT" "$ASSETS/languages.chart"
p

echo
echo '$ ycat demo/assets/ychart/signups.csv           # line'
"$YCAT" "$ASSETS/signups.csv"
p

echo
echo '$ ycat demo/assets/ychart/browsers.json         # pie'
"$YCAT" "$ASSETS/browsers.json"
p

echo
echo '$ ycat demo/assets/ychart/skills.yaml           # radar'
"$YCAT" "$ASSETS/skills.yaml"
p

# Piped through stdin — no extension hint; the `#ychart` directive line is
# sniffed by ycat's content detector.
echo
echo '$ cat demo/assets/ychart/regions.csv | ycat -   # grouped column (sniffed)'
cat "$ASSETS/regions.csv" | "$YCAT" -

printf '\n=== done ===\n'
