#!/bin/bash
# all-interactive.sh — run the interactive demos (own the keyboard / need the
# PTY). Each demo is one `run` line below: comment a line out to skip it.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/all-interactive.sh
#
# ALL_CAP=<seconds>            per-demo hard timeout (default 6)
# ALL_YLESS_DURATION=<seconds> how long each yless view shows before self-exit (default 2)

set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CAP="${ALL_CAP:-6}"

export DEMO_PAUSE=0
export DEMO_HOLD=0
export YLESS_DURATION="${ALL_YLESS_DURATION:-2}"   # yless demos self-exit after this

# Run one demo under a wall-clock cap. Interactive demos keep the inherited PTY
# (no </dev/null): yless needs it for its figure-attach handshake and self-exits
# via YLESS_DURATION; the timeout is the hard backstop. stty sane repairs the
# line discipline after a raw-mode pager.
run() {
    printf '\n===== %s =====\n' "$1"
    timeout -k 2 "$CAP" bash "$DIR/$1" || true
    stty sane 2>/dev/null || true
}

run yless/code.sh
run yless/diagram.sh
run yless/gallery.sh
run yless/music.sh
run yless/pdf.sh
run yless/svg.sh

run ymesh/interactive.sh

# ylottie plays animations in place (blocks, ~60fps clear+redraw until the clip
# ends or Ctrl-C), so it belongs here, not in the scrolling set.
run ylottie/basic.sh
run ylottie/frames.sh

# yctl demos drive a SEPARATE, already-running yetty over RPC (start it with
# `yetty -r <port>`). Uncomment once that server is up.
# run yctl/hello.sh
# run yctl/ctrl-keys.sh
# run yctl/readme-tour.sh
# run yctl/shell-session.sh
# run yctl/typing-styles.sh

printf '\nall-interactive.sh: done\n'
