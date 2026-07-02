#!/bin/bash
# yless demo — a PDF (first page) over the whole pane.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yless/pdf.sh
#
# Quit with q (clears the surface). Multi-page merge into one tall figure is a
# planned follow-up — this shows page 1, centred on the pane.

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

exec "$YLESS" "${dur_args[@]}" "$ROOT/test/ut/ypdf/pdf-sample.pdf"
