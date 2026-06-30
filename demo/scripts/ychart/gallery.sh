#!/bin/bash
# ychart — full kind gallery. Renders every chart family once from the assets
# in demo/assets/ychart/: bar / column (grouped + stacked) / line / area /
# scatter / pie / donut / radar / treemap / sankey.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ychart/gallery.sh

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

p() { sleep "$PAUSE"; }

# asset → one-line description
show() {
    echo
    echo "\$ ycat demo/assets/ychart/$1"
    [ -n "$2" ] && echo "  # $2"
    "$YCAT" "$ASSETS/$1"
    p
}

printf '=== ychart gallery — every chart kind ===\n'
p

show revenue.chart          "column — single series with a value-axis label"
show regions.csv            "column — grouped, three series per category"
show regions-stacked.chart  "column — stacked series"
show languages.chart        "bar — horizontal, value labels on"
show signups.csv            "line — one series with markers"
show traffic.chart          "area — line with translucent fill"
show measurements.json      "scatter — explicit (x, y) points"
show browsers.json          "pie — percentage slices"
show storage.json           "donut — annular slices + centre total"
show skills.yaml            "radar — two series over six axes"
show disk-usage.yaml        "treemap — squarified weighted cells"
show energy.json            "sankey — weighted flow bands between nodes"

printf '\n=== done ===\n'
