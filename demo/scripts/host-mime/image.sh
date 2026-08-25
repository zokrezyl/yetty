#!/bin/bash
# host-mime — raster images decoded by the TERMINAL (yimage/stb), not the
# client. Each file becomes one yimage complex drawable.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/image.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: image (terminal-side decode) ===\n\n'
show "$ASSETS_ROOT/yimage/gradient.png"
show "$ASSETS_ROOT/yimage/rose.png"
show "$ASSETS_ROOT/yimage/hero.png"
show "$ASSETS_ROOT/yimage/wordmark.png"
