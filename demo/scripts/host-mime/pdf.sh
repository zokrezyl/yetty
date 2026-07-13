#!/bin/bash
# host-mime — PDF rendered by the TERMINAL (ypdf via a temp-file spill),
# not the client. One drawable envelope lands per page.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/pdf.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: pdf (terminal-side render) ===\n\n'
show "$ASSETS_ROOT/ypdf/sample.pdf"
show "$ASSETS_ROOT/ypdf/report.pdf"
