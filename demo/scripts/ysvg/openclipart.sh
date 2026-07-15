#!/bin/bash
# openclipart.sh — an endless, shuffled gallery of public-domain clip art from
# Openclipart (openclipart.org), the original community "Flickr for SVG". Each
# round picks a random artwork id and fetches
#   https://openclipart.org/download/<id>
# which redirects to that artwork's SVG file; the file is then rendered through
# ycat's svg handler so it scrolls into the ydraw layer.
#
# Ids are sampled across the whole corpus, so the order is effectively random.
# Openclipart has gaps where art was removed; those ids are skipped silently.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/openclipart.sh
#
# Knobs: DEMO_PAUSE (default 0.5s), DEMO_WIDTH (default 12 cells),
#        DEMO_COUNT (0 = endless). See _infinite.sh.

set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_infinite.sh"

# Artwork ids run from a few hundred to ~340k, with gaps. Sample the range.
# Dead ids (removed art) redirect to the site logo under /assets/images/ —
# reject that so the gallery never shows the same placeholder twice.
ID_MIN=100
ID_SPAN=340000
PLACEHOLDER="/assets/images/"

printf '=== ysvg — Openclipart, endless public-domain SVG clip art ===\n'
printf '    parse failures logged to: %s\n' "$FAIL_LOG"
printf '    failing SVGs saved under: %s/\n' "$FAIL_DIR"

iter=0
misses=0
while more; do
    iter=$((iter + 1))
    id=$(( (RANDOM * 32768 + RANDOM) % ID_SPAN + ID_MIN ))
    dbg "loop iter=$iter shown=$shown_count misses=$misses id=$id"
    show_svg_from_url "https://openclipart.org/download/$id" "openclipart.org #$id" "$PLACEHOLDER"
    rc=$?
    case $rc in
        0) dbg "outcome: RENDERED"; misses=0 ;;
        2) dbg "outcome: skipped (placeholder/unparseable)" ;;   # don't count
        *) misses=$((misses + 1))                                # hard failure (network?)
           dbg "outcome: HARD FAIL (misses=$misses/8)"
           if [ "$misses" -ge 8 ]; then
               echo "Too many failures in a row — Openclipart unreachable or network down." >&2
               exit 0
           fi ;;
    esac
done
