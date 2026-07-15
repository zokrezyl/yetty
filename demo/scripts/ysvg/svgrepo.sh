#!/bin/bash
# svgrepo.sh — an endless, shuffled gallery of SVGs from SVG Repo
# (svgrepo.com), a huge library of free vectors and icons. The pool of vectors
# is discovered once from the site's sitemap and cached under tmp/; each round
# then fetches one at random and renders it through ycat's svg handler.
#
# NOTE: svgrepo.com sits behind Cloudflare bot protection. From an ordinary
# desktop network it serves fine, but from data-centre / VPN / cloud IPs
# Cloudflare answers every request with HTTP 429. When that happens (or the
# sitemap lists no individual vectors) this script prints a short notice and
# exits cleanly — run it from an unblocked network.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/svgrepo.sh
#
# Knobs: DEMO_PAUSE (default 0.5s), DEMO_WIDTH (default 12 cells),
#        DEMO_COUNT (0 = endless). See _infinite.sh.

set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_infinite.sh"

CACHE="$TMP/ysvg-idx-svgrepo.txt"

# Walk the sitemap (index -> sub-sitemaps), collect /svg/<id>/<slug> vector
# pages and map each to its /download/<id>/<slug>.svg file. Prints one URL per
# line; empty output when Cloudflare blocks us or no vectors are listed.
build_svgrepo_index() {
    curl -s --max-time 30 -L -A "$BROWSER_UA" \
        -H 'Accept: application/xml,text/xml,*/*;q=0.8' \
        "https://www.svgrepo.com/sitemap.xml" \
      | python3 -c '
import sys, re, io, gzip, urllib.request

browser_ua = sys.argv[1]

def fetch(url):
    request = urllib.request.Request(
        url, headers={"User-Agent": browser_ua,
                      "Accept": "application/xml,*/*;q=0.8"})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read()
    if url.endswith(".gz") or data[:2] == b"\x1f\x8b":
        data = gzip.GzipFile(fileobj=io.BytesIO(data)).read()
    return data.decode("utf-8", "replace")

vectors = set()

def harvest(text):
    for vid, slug in re.findall(r"https?://[^<\s]+/svg/(\d+)/([^<\s/]+)", text):
        vectors.add((vid, slug))

index = sys.stdin.read()
harvest(index)

sub_sitemaps = [u for u in re.findall(r"<loc>\s*([^<\s]+)</loc>", index)
                if "sitemap" in u.lower()]
for sitemap_url in sub_sitemaps[:40]:
    try:
        harvest(fetch(sitemap_url))
    except Exception:
        pass
    if len(vectors) >= 5000:
        break

for vid, slug in vectors:
    print("https://www.svgrepo.com/download/%s/%s.svg" % (vid, slug))
' "$BROWSER_UA"
}

printf '=== ysvg — SVG Repo, endless free-vector SVG gallery ===\n'

if [ ! -s "$CACHE" ]; then
    build_svgrepo_index > "$CACHE" 2>/dev/null || true
fi
if [ ! -s "$CACHE" ]; then
    echo "Could not enumerate svgrepo.com — it is almost certainly behind" >&2
    echo "Cloudflare (HTTP 429) from this network. Try the demo from an" >&2
    echo "ordinary desktop network where svgrepo.com loads in a browser." >&2
    exit 0
fi

misses=0
while more; do
    url="$(random_line "$CACHE")" || break
    if show_svg_from_url "$url" "svgrepo.com"; then
        misses=0
    else
        misses=$((misses + 1))
        if [ "$misses" -ge 12 ]; then
            echo "Too many misses in a row — svgrepo.com blocked (Cloudflare?) or down." >&2
            exit 0
        fi
    fi
done
