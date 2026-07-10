#!/bin/bash
# Records the "PDF in scrollback" clip end to end: stages report.pdf,
# launches a dedicated yetty --record instance, plays the scenario, and
# leaves the finalized MP4 in the staging directory:
#
#   ./demo/scripts/yctl/clips/pdf-in-scrollback.sh
#   # -> tmp/clips/pdf-in-scrollback/pdf-in-scrollback.mp4
#
# The staged report.pdf defaults to the 3-page generated report
# (demo/assets/ypdf/report.pdf, from make-report.py in the same directory).
# To record with a different document, point REPORT_PDF at it — the
# scenario itself never changes:
#
#   REPORT_PDF=~/papers/some-report.pdf ./demo/scripts/yctl/clips/pdf-in-scrollback.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/pdf-in-scrollback.yaml"
STAGING_DIR="$ROOT/tmp/clips/pdf-in-scrollback"
REPORT_PDF="${REPORT_PDF:-$ROOT/demo/assets/ypdf/report.pdf}"

if [ ! -f "$REPORT_PDF" ]; then
    echo "report PDF not found: $REPORT_PDF" >&2
    exit 1
fi

mkdir -p "$STAGING_DIR"
cp "$REPORT_PDF" "$STAGING_DIR/report.pdf"

record_clip "$SCENARIO" "$STAGING_DIR" "pdf-in-scrollback.mp4"
