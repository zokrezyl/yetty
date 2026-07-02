#!/bin/bash
# yless demo — a mermaid class diagram (chart-like content) over the pane.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yless/diagram.sh
#
# Stands in as the chart/"plot" example until yplot output can be rendered
# into a yview figure. Quit with q (clears the surface).

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

# all.sh exports YLESS_DURATION so the demo runs for a fixed time and exits on
# its own (clearing the surface). Unset when run standalone → interactive (q).
dur_args=()
if [ -n "${YLESS_DURATION:-}" ]; then
    dur_args=(--duration "$YLESS_DURATION")
fi

# Whole pane: the diagram engraves at its natural size, anchored top-left.
exec "$YLESS" "${dur_args[@]}" "$ROOT/demo/assets/ydiagram/class-diagram.mmd"
