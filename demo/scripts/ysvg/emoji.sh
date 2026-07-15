#!/bin/bash
# emoji.sh — an endless, shuffled gallery of full-colour emoji SVGs streamed
# from open emoji sets on the jsDelivr CDN (OpenMoji, Twemoji). Thousands of
# vector emoji, shown in random order, forever.
#
# The file list of each set is fetched once from jsDelivr's data API and cached
# under tmp/; every round then picks a random SVG and renders it through ycat's
# svg handler so it scrolls into the ydraw layer.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/emoji.sh
#
# Knobs: DEMO_PAUSE (default 2s between figures), DEMO_COUNT (0 = endless).

set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_infinite.sh"

# "npm-package|path-prefix|label" — full-colour emoji sets only.
SOURCES=(
    "openmoji|/color/svg/|OpenMoji (color)"
    "@twemoji/svg|/|Twemoji"
)

printf '=== ysvg — emoji, an endless full-colour SVG gallery ===\n'

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
