#!/bin/bash
# bioicons.sh — an endless, shuffled gallery from Bioicons (bioicons.com), a
# library of a few thousand biological / scientific SVG icons (cells, molecules,
# lab apparatus, organisms). Useful for a science-oriented renderer workout.
#
# The catalogue (icons.json) is fetched once and turned into download URLs of
# the shape https://bioicons.com/icons/<license>/<category>/<author>/<name>.svg,
# cached under tmp/. A minority of entries 404 (author/name slug quirks); those
# are skipped.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/bioicons.sh
#
# Knobs: DEMO_PAUSE (default 0.5s), DEMO_WIDTH (default 12), DEMO_COUNT (0 = endless).

set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_infinite.sh"

CATALOGUE="https://bioicons.com/icons/icons.json"
CACHE="$TMP/bioicons-urls.txt"

# ensure_index — build the cache of per-icon download URLs from icons.json.
ensure_index() {
    [ -s "$CACHE" ] && return 0
    curl -s --max-time 30 -A "$BROWSER_UA" "$CATALOGUE" \
      | python3 -c 'import sys, json, urllib.parse
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
for entry in data:
    try:
        parts = [entry["license"], entry["category"], entry["author"],
                 entry["name"] + ".svg"]
    except Exception:
        continue
    print("https://bioicons.com/icons/"
          + "/".join(urllib.parse.quote(p) for p in parts))' > "$CACHE" 2>/dev/null || true
    [ -s "$CACHE" ]
}

printf '=== ysvg — Bioicons, an endless scientific SVG gallery ===\n'
printf '    parse failures logged to: %s\n' "$FAIL_LOG"

misses=0
hardfails=0
while more; do
    if ! ensure_index; then
        misses=$((misses + 1))
        [ "$misses" -ge 3 ] && { echo "Bioicons unreachable — is the network up?" >&2; exit 0; }
        continue
    fi
    misses=0
    url="$(random_line "$CACHE")" || continue
    show_svg_from_url "$url" "Bioicons"
    case $? in
        0 | 2) hardfails=0 ;;
        *) hardfails=$((hardfails + 1))
           [ "$hardfails" -ge 10 ] && { echo "Too many fetch failures — network down?" >&2; exit 0; } ;;
    esac
done
