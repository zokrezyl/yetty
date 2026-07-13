#!/bin/bash
# host-mime — markdown rendered by the TERMINAL (ymarkdown), not the client.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/markdown.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: markdown (terminal-side render) ===\n\n'
show "$ASSETS_ROOT/ymarkdown/overview.md"
show "$ASSETS_ROOT/ymarkdown/cheatsheet.md"
