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
    YLESS="$(command -v "${YLESS##*/}" 2>/dev/null || true)"
fi
if [ -z "$YLESS" ] || [ ! -x "$YLESS" ]; then
    echo "yless binary not found in build dir or on \$PATH — set YLESS=path/to/yless" >&2
    exit 1
fi

# all.sh exports YLESS_DURATION so the demo runs for a fixed time and exits on
# its own (clearing the surface). Unset when run standalone → interactive (q).
dur_args=()
if [ -n "${YLESS_DURATION:-}" ]; then
    dur_args=(--duration "$YLESS_DURATION")
fi

exec "$YLESS" "${dur_args[@]}" "$ROOT/demo/assets/music/ode-to-joy.ly"
