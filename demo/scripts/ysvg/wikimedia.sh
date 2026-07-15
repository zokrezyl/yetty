#!/bin/bash
# wikimedia.sh — an endless, shuffled gallery of real SVG artwork pulled live
# from Wikimedia Commons: flags, coats of arms, maps, diagrams, silhouettes,
# logos, seals and illustrations — the closest thing to a "Flickr for SVG".
#
# Each round asks the Commons search API for SVG files (filemime:image/svg+xml)
# at a random offset, optionally narrowed by a topic, then renders a random one
# of that batch through ycat's svg handler so it scrolls into the ydraw layer.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/wikimedia.sh
#   ./build-desktop-ytrace-release/yetty -e 'demo/scripts/ysvg/wikimedia.sh flag'
#
# Knobs: DEMO_PAUSE (default 2s between figures), DEMO_COUNT (0 = endless).

set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_infinite.sh"

API="https://commons.wikimedia.org/w/api.php"

# When no topic is given on the command line, rotate through art-rich ones.
TOPICS=("" flag "coat of arms" map silhouette illustration logo emblem \
        diagram heraldry seal pattern poster mandala icon)

USER_TOPIC="${1:-}"

# pick_svg TOPIC OFFSET — print one random SVG file URL from Commons, or nothing.
pick_svg() {
    local topic="$1" offset="$2"
    curl -sG --max-time 20 -A "$USER_AGENT" "$API" \
        --data-urlencode "action=query" \
        --data-urlencode "format=json" \
        --data-urlencode "generator=search" \
        --data-urlencode "gsrsearch=filemime:image/svg+xml $topic" \
        --data-urlencode "gsrnamespace=6" \
        --data-urlencode "gsrlimit=12" \
        --data-urlencode "gsroffset=$offset" \
        --data-urlencode "prop=imageinfo" \
        --data-urlencode "iiprop=url|mime" \
      | python3 -c 'import sys, json, random
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
pages = data.get("query", {}).get("pages", {})
urls = [p["imageinfo"][0]["url"] for p in pages.values()
        if p.get("imageinfo") and p["imageinfo"][0].get("mime") == "image/svg+xml"]
if urls:
    print(random.choice(urls))'
}

printf '=== ysvg — Wikimedia Commons, an endless SVG gallery ===\n'
[ -n "$USER_TOPIC" ] && printf '    topic filter: %s\n' "$USER_TOPIC"

misses=0
while more; do
    if [ -n "$USER_TOPIC" ]; then
        topic="$USER_TOPIC"
    else
        topic="${TOPICS[$(( RANDOM % ${#TOPICS[@]} ))]}"
    fi
    # Commons caps deep search paging; stay well inside the window.
    offset=$(( RANDOM % 8000 ))

    url="$(pick_svg "$topic" "$offset")"
    if [ -z "$url" ]; then
        misses=$((misses + 1))
        if [ "$misses" -ge 5 ]; then
            echo "No results / Commons unreachable — is the network up?" >&2
            exit 0
        fi
        continue
    fi
    misses=0
    show_url "$url" "${topic:-any} @ commons.wikimedia.org" || true
done
