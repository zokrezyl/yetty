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
    YLESS="$(command -v "${YLESS##*/}" 2>/dev/null || true)"
fi
if [ -z "$YLESS" ] || [ ! -x "$YLESS" ]; then
    echo "yless binary not found in build dir or on \$PATH — set YLESS=path/to/yless" >&2
    exit 1
fi

# all.sh exports YLESS_DURATION so each view runs for a fixed time and exits on
# its own (advancing to the next). Unset when run standalone → fully
# interactive: quit each view with q to advance.
dur_args=()
if [ -n "${YLESS_DURATION:-}" ]; then
    dur_args=(--duration "$YLESS_DURATION")
fi

step() {
    printf '\n=== %s ===  (scroll: j/k Space/b g/G ; quit: q to advance)\n' "$1"
}

# Position/size use pane percentages so each box lands correctly whatever the
# window size (a raw cell count assumes a fixed grid and drifts on resize).
step "source code — whole pane"
"$YLESS" "${dur_args[@]}" "$ROOT/tools/yless/main.c"

step "svg — left half"
"$YLESS" "${dur_args[@]}" -x 4% -y 8% -w 46% -H 78% "$ROOT/demo/assets/svg/tiger.svg"

step "mermaid diagram — whole pane"
"$YLESS" "${dur_args[@]}" "$ROOT/demo/assets/ydiagram/class-diagram.mmd"

step "pdf (page 1) — whole pane"
"$YLESS" "${dur_args[@]}" "$ROOT/test/ut/ypdf/pdf-sample.pdf"

printf '\n=== gallery done ===\n'
