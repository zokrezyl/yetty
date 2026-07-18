#!/usr/bin/env bash
#
# ydoc / yrich UI capture harness.
#
# Makes in-terminal and standalone UI testing repeatable instead of guesswork:
#   * launches a dedicated yetty-under-test on its own RPC port,
#   * finds THAT window by pid (import -window root grabs the whole desktop —
#     the wrong thing),
#   * launches a normal shell, `clear`s it so the figure origin is fixed, then
#     types the tool at the prompt (matching real usage — launching the tool as
#     the pane leader via `-e` deadlocks the in-terminal ywire path),
#   * drives it through yctl (clicks/keys) and captures labelled PNGs,
#   * tears down only the pid it started (never a pattern kill).
#
# Usage:
#   qa-tools/ydoc-ui/capture.sh terminal   [ydoc|ysheet|yslide]
#   qa-tools/ydoc-ui/capture.sh standalone [ydoc|ysheet|yslide]
#   qa-tools/ydoc-ui/capture.sh drive      [ydoc|ysheet|yslide]
#
# Output PNGs land in tmp/ydoc-ui/. Requires xdotool + ImageMagick `import`.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/build-desktop-ytrace-release"
YETTY="$BUILD/yetty"
PORT="${PORT:-9970}"
OUT="$ROOT/tmp/ydoc-ui"
YCTL="uv run $ROOT/tools/yctl-client/yctl.py -p $PORT"
mkdir -p "$OUT"

MODE="${1:-terminal}"
TOOL="${2:-ydoc}"
BIN="$BUILD/tools/$TOOL/$TOOL"

YPID=""   # set by launch_*, the only process this script kills
WID=""    # the under-test window id

log()  { printf '  %s\n' "$*" >&2; }
yctl() { $YCTL "$@" >/dev/null 2>&1 || true; }

# CRITICAL: `import -window ""` (empty id) drops into interactive mode and GRABS
# the X pointer, freezing every later xdotool/import until killed. Never call it
# without a concrete window id, and always cap it with `timeout`.
shot() {
    if [ -z "$WID" ]; then log "shot: no window id, skipping $1"; return 1; fi
    timeout 10 import -window "$WID" "$OUT/$1.png" 2>/dev/null &&
        log "shot -> tmp/ydoc-ui/$1.png"
}

find_window() { # poll for the window owned by $YPID
    for _ in $(seq 1 30); do
        WID="$(timeout 5 xdotool search --pid "$YPID" 2>/dev/null | tail -1 || true)"
        [ -n "$WID" ] && { timeout 5 xdotool windowactivate "$WID" 2>/dev/null; return 0; }
        sleep 0.3
    done
    log "no window appeared for pid $YPID"; return 1
}

launch_terminal() {
    log "launch: yetty -r $PORT, then run $TOOL at the shell prompt (in-terminal)"
    "$YETTY" -r "$PORT" >"$OUT/yetty-$TOOL.log" 2>&1 &
    YPID=$!
    find_window || return 1
    sleep 3
    yctl run "clear"; sleep 1          # fixed figure origin, no scrollback drift
    yctl run "$BIN"; sleep 4           # figure appears at the (cleared) prompt row
}

launch_standalone() {
    log "launch: $TOOL standalone GPU window"
    env -u TERM_PROGRAM -u TMUX "$BIN" >"$OUT/$TOOL-standalone.log" 2>&1 &
    YPID=$!
    find_window || return 1
    sleep 3
}

teardown() { [ -n "$YPID" ] && kill "$YPID" 2>/dev/null; }
trap teardown EXIT

case "$MODE" in
terminal)
    launch_terminal || exit 1
    shot "$TOOL-terminal"
    ;;
standalone)
    launch_standalone || exit 1
    shot "$TOOL-standalone"
    ;;
drive)
    launch_terminal || exit 1
    shot "$TOOL-01-open"
    yctl mouse-down 200 200; yctl mouse-up 200 200; sleep 1     # focus + caret
    yctl type "Hello from the UI harness."; sleep 1
    shot "$TOOL-02-typed"
    yctl char 97 --mods 2; sleep 1                              # Ctrl+A select all
    yctl char 98 --mods 2; sleep 1                              # Ctrl+B bold
    shot "$TOOL-03-bold"
    ;;
*)
    echo "usage: $0 {terminal|standalone|drive} [ydoc|ysheet|yslide]" >&2
    exit 2
    ;;
esac
