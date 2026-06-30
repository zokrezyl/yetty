#!/bin/bash
# yless demo — a PDF (first page) in a centred box.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yless/pdf.sh
#
# Quit with q (clears the surface). Multi-page merge into one tall figure is a
# planned follow-up — this shows page 1.

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

exec "$YLESS" -x 16 -y 4 -w 48 -H 28 "$ROOT/test/ut/ypdf/pdf-sample.pdf"
