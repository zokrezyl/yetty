#!/bin/bash
# ytop — live system monitor rendered as a ygui figure in the host yetty:
# per-core CPU bars across the header, plus a process TABLE sorted by CPU
# that refreshes on a libuv timer. Reads /proc, so it is Linux-only.
#
# Usage — run it directly in a real yetty window (NOT via the automated tour):
#   yetty -e demo/scripts/ytop/basic.sh
#
# Controls:
#   q / ESC   quit (clears the surface)
#
# Renders in a live yetty window (verified: its widgets ship over the client OSC
# envelope). It is kept OUT of the scripted all-interactive.sh tour only because
# that tour drives every demo through a nested, non-interactive `-e` pipe where a
# ygui client cannot get a stable pane size; run it directly and it draws fine.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YTOP="${YTOP:-$ROOT/build-desktop-ytrace-release/tools/ytop/ytop}"

if [ "$(uname)" != "Linux" ]; then
    echo "ytop reads /proc — Linux only. Skipping on $(uname)." >&2
    exit 0
fi

if [ ! -x "$YTOP" ]; then
    YTOP="$(command -v "${YTOP##*/}" 2>/dev/null || true)"
fi
if [ -z "$YTOP" ] || [ ! -x "$YTOP" ]; then
    echo "ytop binary not found in build dir or on \$PATH — set YTOP=path/to/ytop" >&2
    exit 1
fi

printf '=== ytop — live per-core CPU + process table ===\n'
printf 'q / ESC = quit\n\n'

exec "$YTOP"
