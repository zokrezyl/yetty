#!/bin/bash
# icons.sh — an endless, shuffled gallery of vector icon / logo art streamed
# from open icon sets on the jsDelivr CDN: Tabler, Material Design Icons,
# Simple Icons (brand logos), Bootstrap Icons and Lucide. Tens of thousands of
# SVGs between them, shown in random order, forever.
#
# The file list of each set is fetched once from jsDelivr's data API and cached
# under tmp/; every round then picks a random SVG and renders it through ycat's
# svg handler so it scrolls into the ydraw layer.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/icons.sh
#
# Knobs: DEMO_PAUSE (default 2s between figures), DEMO_COUNT (0 = endless).

set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_infinite.sh"

# "npm-package|path-prefix|label"
SOURCES=(
    "@tabler/icons|/icons/|Tabler"
    "@mdi/svg|/svg/|Material Design Icons"
    "simple-icons|/icons/|Simple Icons (brand logos)"
    "bootstrap-icons|/icons/|Bootstrap Icons"
    "lucide-static|/icons/|Lucide"
)

printf '=== ysvg — icons & logos, an endless SVG gallery ===\n'

misses=0
while more; do
    entry="${SOURCES[$(( RANDOM % ${#SOURCES[@]} ))]}"
    pkg="${entry%%|*}"
    rest="${entry#*|}"
    prefix="${rest%%|*}"
    label="${rest#*|}"

    cache="$TMP/ysvg-idx-${pkg//[^A-Za-z0-9]/_}.txt"
    if ! cache_index "$cache" "$pkg" "$prefix"; then
        misses=$((misses + 1))
        if [ "$misses" -ge 3 ]; then
            echo "jsDelivr unreachable — is the network up?" >&2
            exit 0
        fi
        continue
    fi
    misses=0
    url="$(random_line "$cache")" || continue
    show_url "$url" "$label" || true
done
