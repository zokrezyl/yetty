#!/bin/bash
# host-mime — LilyPond scores engraved by the TERMINAL (ymusic), not the
# client. Systems wrap to the pane width at the terminal's text scale.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/music.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: music (terminal-side engraving) ===\n\n'
show "$ASSETS_ROOT/music/c-major-scale.ly"
show "$ASSETS_ROOT/music/chords.ly"
show "$ASSETS_ROOT/music/ode-to-joy.ly"
