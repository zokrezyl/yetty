#!/bin/bash
# Musical-Score Plugin: Sheet music display with configurable staves
#
# Usage: ./demo/scripts/musical-score.sh [staves] [width] [height]
#   staves: 1-16 (default: 4)
#   width:  terminal cells (default: 80)
#   height: terminal cells (default: 30)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

STAVES="${1:-4}"
WIDTH="${2:-80}"
HEIGHT="${3:-30}"

cd "$ROOT_DIR"
uv run python3 tools/yetty-client/main.py create musical-score \
    -s "$STAVES" \
    -w "$WIDTH" -H "$HEIGHT"
