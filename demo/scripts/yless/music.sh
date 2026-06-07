#!/bin/bash
# yless demo — engrave a LilyPond score and page through it.
#
# yless detects the .ly extension and renders the score via ymusic: clefs,
# noteheads, rests and accidentals are Emmentaler music-font glyphs; staff
# lines, stems, ledger lines and barlines are SDF primitives. The score wraps
# into systems sized to the pane and scrolls vertically.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yless/music.sh
#
# Keys: j/k or arrows scroll a line, Space/b page, g/G jump. Quit with q.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YLESS="${YLESS:-$ROOT/build-desktop-ytrace-release/tools/yless/yless}"

if [ ! -x "$YLESS" ]; then
    echo "yless binary not found at $YLESS — set YLESS=path/to/yless" >&2
    exit 1
fi

exec "$YLESS" "$ROOT/demo/assets/music/ode-to-joy.ly"
