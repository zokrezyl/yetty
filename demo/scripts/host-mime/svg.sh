#!/bin/bash
# host-mime — SVG rendered by the TERMINAL (ysvg), not the client.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/svg.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: svg (terminal-side render) ===\n\n'
show "$ASSETS_ROOT/svg/svg-logo.svg"
show "$ASSETS_ROOT/svg/smiley-green-alien.svg"
show "$ASSETS_ROOT/svg/tiger.svg"
