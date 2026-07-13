#!/bin/bash
# host-mime — an Excel workbook rendered by the TERMINAL (ymsoffice), not
# the client: the raw xlsx container goes over the wire and yetty draws the
# sheets as bordered grids with A/1 headers and right-aligned numbers.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/xlsx.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: xlsx (terminal-side render) ===\n\n'
show "$ASSETS_ROOT/ymsoffice/budget.xlsx"
