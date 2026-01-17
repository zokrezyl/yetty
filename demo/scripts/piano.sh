#!/bin/bash
# Piano Plugin: Interactive keyboard with mouse/keyboard input, MIDI note display
#
# Usage: ./demo/scripts/piano.sh [octaves] [start_octave]
#   octaves: 1-8 (default: 2)
#   start_octave: 0-9 (default: 4, middle C)
#
# Controls:
#   Mouse: Click keys to play
#   Keyboard: A-L = white keys (C to B)
#             W,E,T,Y,U,O,P = black keys

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

OCTAVES="${1:-2}"
START="${2:-4}"
WIDTH="${3:-80}"
HEIGHT="${4:-15}"

cd "$ROOT_DIR"
uv run python3 tools/yetty-client/main.py create piano \
    --octaves "$OCTAVES" --start "$START" \
    -w "$WIDTH" -H "$HEIGHT"
