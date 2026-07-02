#!/bin/bash
# ysvg — full gallery. Renders every SVG asset in demo/assets/svg/ once, from a
# simple flag to the W3C acid test, exercising gradients, transforms, text and
# dense path sets through ycat's svg handler.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/gallery.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/svg"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YCAT" ]; then
    YCAT="$(command -v "${YCAT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YCAT" ] || [ ! -x "$YCAT" ]; then
    echo "ycat binary not found in build dir or on \$PATH — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

show() {
    echo
    echo "\$ ycat demo/assets/svg/$1"
    [ -n "$2" ] && echo "  # $2"
    "$YCAT" "$ASSETS/$1"
    p
}

printf '=== ysvg gallery ===\n'
p

show de-flag.svg              "flat rectangles — the simplest case"
show svg-logo.svg             "gradients and paths"
show smiley-green-alien.svg   "filled organic shapes"
show firefox-logo.svg         "layered gradients"
show smoke.svg                "translucent blurred paths"
show tiger.svg                "the classic path-heavy benchmark"
show periodic-table-large.svg "lots of small text + cells"
show w3c-acid.svg             "the W3C acid conformance test"

printf '\n=== done ===\n'
