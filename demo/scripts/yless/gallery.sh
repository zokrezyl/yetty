#!/bin/bash
# yless demo — gallery: each content type at a different position, one after
# another. Quit each view (q) to advance to the next.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yless/gallery.sh
#
# yless is interactive and owns the keyboard, so the views are shown
# sequentially rather than all at once. Drawing many views to many positions
# *simultaneously* is the yview-library use case (one controlling program),
# e.g. the planned nvim plugin.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YLESS="${YLESS:-$ROOT/build-desktop-ytrace-release/tools/yless/yless}"

if [ ! -x "$YLESS" ]; then
    echo "yless binary not found at $YLESS — set YLESS=path/to/yless" >&2
    exit 1
fi

step() {
    printf '\n=== %s ===  (scroll: j/k Space/b g/G ; quit: q to advance)\n' "$1"
}

step "source code — whole pane"
"$YLESS" "$ROOT/tools/yless/main.c"

step "svg — 48x24 box, top-left"
"$YLESS" -x 0 -y 0 -w 48 -H 24 "$ROOT/demo/assets/svg/tiger.svg"

step "mermaid diagram — right-hand half"
"$YLESS" -x 42 -w 38 "$ROOT/demo/assets/ydiagram/class-diagram.mmd"

step "pdf (page 1) — centred box"
"$YLESS" -x 16 -y 4 -w 48 -H 28 "$ROOT/test/ut/ypdf/pdf-sample.pdf"

printf '\n=== gallery done ===\n'
