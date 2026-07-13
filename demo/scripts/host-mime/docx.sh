#!/bin/bash
# host-mime — a Word document rendered by the TERMINAL (ymsoffice), not the
# client: the raw docx container goes over the wire and yetty parses the
# ZIP+XML and lays out headings, styled runs, lists and tables itself.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/docx.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: docx (terminal-side render) ===\n\n'
show "$ASSETS_ROOT/ymsoffice/report.docx"
