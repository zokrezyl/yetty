#!/bin/bash
# svg-testsuite.sh — an endless, shuffled gallery of SVG *conformance* test
# cases, aimed squarely at stressing the renderer rather than showing pretty
# art. Source: the resvg test suite (github.com/RazrFalcon/resvg-test-suite),
# ~1600 hand-crafted SVGs covering shapes, paths, painting, gradients, text,
# masking, clipping, transforms and structural edge cases — the same corpus a
# standalone SVG renderer is validated against. (For full spec conformance see
# also the W3C SVG Test Suite; this is the practical renderer-oriented set.)
#
# Files come from the jsDelivr GitHub CDN; the file list is fetched once and
# cached. SVGs our parser rejects are logged to the failures file — which is the
# whole point here.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/svg-testsuite.sh
#
# Knobs: DEMO_PAUSE (default 0.5s), DEMO_WIDTH (default 12), DEMO_COUNT (0 = endless).

set -u
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_infinite.sh"

GH_OWNER="RazrFalcon"
GH_REPO="resvg-test-suite"
GH_REF="master"
CACHE="$TMP/svg-testsuite-urls.txt"

# ensure_index — cache CDN URLs of every test-case .svg in the suite.
ensure_index() {
    [ -s "$CACHE" ] && return 0
    curl -s --max-time 30 -A "$USER_AGENT" \
        "https://data.jsdelivr.com/v1/packages/gh/$GH_OWNER/$GH_REPO@$GH_REF?structure=flat" \
      | GH_OWNER="$GH_OWNER" GH_REPO="$GH_REPO" GH_REF="$GH_REF" python3 -c 'import sys, os, json
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
base = "https://cdn.jsdelivr.net/gh/%s/%s@%s" % (
    os.environ["GH_OWNER"], os.environ["GH_REPO"], os.environ["GH_REF"])
for entry in data.get("files", []):
    name = entry.get("name", "")
    if name.startswith("/tests/") and name.endswith(".svg"):
        # "#" in a filename is a URL fragment delimiter — encode it (and space)
        # so curl fetches the file, not a truncated path.
        print(base + name.replace("#", "%23").replace(" ", "%20"))' > "$CACHE" 2>/dev/null || true
    [ -s "$CACHE" ]
}

printf '=== ysvg — SVG renderer conformance test suite (endless) ===\n'
printf '    parse failures logged to: %s\n' "$FAIL_LOG"

misses=0
hardfails=0
while more; do
    if ! ensure_index; then
        misses=$((misses + 1))
        [ "$misses" -ge 3 ] && { echo "jsDelivr unreachable — is the network up?" >&2; exit 0; }
        continue
    fi
    misses=0
    url="$(random_line "$CACHE")" || continue
    show_svg_from_url "$url" "resvg test suite"
    case $? in
        0 | 2) hardfails=0 ;;
        *) hardfails=$((hardfails + 1))
           [ "$hardfails" -ge 8 ] && { echo "Too many fetch failures — network down?" >&2; exit 0; } ;;
    esac
done
