#!/bin/bash
# iconify.sh — an endless, shuffled gallery from Iconify (api.iconify.design),
# which aggregates hundreds of open-source icon sets (tens of thousands of
# icons). Each round picks a random set, then a random icon from it, and renders
# it through ycat's svg handler. Great for stress-testing path/stroke rendering
# across many authoring styles.
#
# The set list (and each set's icon-name list) is fetched once from the Iconify
# API and cached under tmp/; SVGs come from https://api.iconify.design/<set>/<name>.svg
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/iconify.sh
#
# Knobs: DEMO_PAUSE (default 0.5s), DEMO_WIDTH (default 12), DEMO_COUNT (0 = endless).

set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_infinite.sh"

API="https://api.iconify.design"
PREFIX_CACHE="$TMP/iconify-prefixes.txt"

# ensure_prefixes — cache the list of every Iconify set prefix.
ensure_prefixes() {
    [ -s "$PREFIX_CACHE" ] && return 0
    curl -s --max-time 20 -A "$USER_AGENT" "$API/collections" \
      | python3 -c 'import sys, json
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
for prefix in data:
    print(prefix)' > "$PREFIX_CACHE" 2>/dev/null || true
    [ -s "$PREFIX_CACHE" ]
}

# ensure_names SET CACHEFILE — cache the icon-name list of one set.
ensure_names() {
    local set="$1" cache="$2"
    [ -s "$cache" ] && return 0
    curl -s --max-time 25 -A "$USER_AGENT" "$API/collection?prefix=$set" \
      | python3 -c 'import sys, json
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
names = list(data.get("uncategorized") or [])
for group in (data.get("categories") or {}).values():
    names += group
for name in names:
    print(name)' > "$cache" 2>/dev/null || true
    [ -s "$cache" ]
}

printf '=== ysvg — Iconify, an endless multi-set SVG icon gallery ===\n'
printf '    parse failures logged to: %s\n' "$FAIL_LOG"

misses=0
hardfails=0
while more; do
    if ! ensure_prefixes; then
        misses=$((misses + 1))
        [ "$misses" -ge 3 ] && { echo "Iconify API unreachable — is the network up?" >&2; exit 0; }
        continue
    fi
    misses=0
    set="$(random_line "$PREFIX_CACHE")" || continue
    names_cache="$TMP/iconify-${set//[^A-Za-z0-9]/_}.txt"
    ensure_names "$set" "$names_cache" || continue
    name="$(random_line "$names_cache")" || continue

    show_svg_from_url "$API/$set/$name.svg" "Iconify · $set/$name"
    case $? in
        0 | 2) hardfails=0 ;;
        *) hardfails=$((hardfails + 1))
           [ "$hardfails" -ge 8 ] && { echo "Too many fetch failures — network down?" >&2; exit 0; } ;;
    esac
done
