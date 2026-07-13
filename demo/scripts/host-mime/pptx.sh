#!/bin/bash
# host-mime — a PowerPoint deck rendered by the TERMINAL (ymsoffice), not
# the client: the raw pptx container goes over the wire and yetty draws one
# scaled slide panel per slide with its shapes and text inside.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/pptx.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: pptx (terminal-side render) ===\n\n'
show "$ASSETS_ROOT/ymsoffice/pitch.pptx"
