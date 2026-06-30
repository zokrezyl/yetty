#!/bin/bash
# all.sh — run every demo back-to-back, fast, each under a hard timeout.
#
# Built to stress a single yetty instance: run it as yetty's launch command so
# every demo emits its OSC content into the same terminal —
#
#   ./build-desktop-ytrace-debug/yetty -e demo/scripts/all.sh
#
# or under gdb to catch a crash with a backtrace —
#
#   gdb -batch -ex run -ex 'thread apply all bt' \
#       --args ./build-desktop-ytrace-debug/yetty -e demo/scripts/all.sh
#
# Every demo runs with DEMO_PAUSE=0 (no inter-step sleeps) and a per-demo
# wall-clock cap, so the holds ("sleep 600"), the yless pagers and the
# interactive viewers are all cut short and the whole set runs through quickly.
#
# Knobs (environment):
#   ALL_CAP=3         per-demo wall-clock cap in seconds
#   ALL_YLESS_DURATION=2  seconds each yless demo displays before self-exiting
#                         (passed as yless --duration; keep below ALL_CAP so a
#                         single-view yless demo clears itself before the cap)
#   ALL_REPEAT=1      run the whole set this many times (stress repetition)
#   ALL_BUILD=build-desktop-ytrace-debug   build dir whose tools the demos use
#   ALL_YCTL=0        set 1 to also run yctl/* (needs a yetty started with -r)
#   ALL_DRYRUN=0      set 1 to list the demos that would run, then exit
#   ALL_PROGRESS=<file>   progress log (default <root>/tmp/all-progress.log)

set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

CAP="${ALL_CAP:-3}"
YLESS_DURATION="${ALL_YLESS_DURATION:-2}"
REPEAT="${ALL_REPEAT:-1}"
BUILD="${ALL_BUILD:-build-desktop-ytrace-debug}"
RUN_YCTL="${ALL_YCTL:-0}"
DRYRUN="${ALL_DRYRUN:-0}"
PROGRESS="${ALL_PROGRESS:-$ROOT/tmp/all-progress.log}"

# No inter-step pauses anywhere.
export DEMO_PAUSE=0

# Point the demos' overridable tool variables at the chosen build dir, so the
# run exercises that build's tools. Each demo still falls back to its own
# default / $PATH if a binary is missing. (The yrich demos hardcode their tool
# path and ignore these — that is fine, they still run.)
bindir="$ROOT/$BUILD/tools"
export YPLOT="$bindir/yplot/yplot"
export YCAT="$bindir/ycat/ycat"
export YLESS="$bindir/yless/yless"
export YFSPY="$bindir/yfspy/yfspy"
export YECHO="$bindir/yecho/yecho"
export YMESH="$bindir/ymesh/ymesh"
export YLOTTIE="$bindir/ylottie/yetty-ylottie"
export YDIAGRAM="$bindir/ydiagram/ydiagram"
export YVIDEO="$bindir/yvideo/yvideo"
export YTHORVG="$bindir/ythorvg/yetty-ythorvg"
export YMAP="$bindir/ymap/ymap"
export YFLAME="$bindir/yflame/yflame"

# yless self-exits after this many seconds (its --duration). The yless demos
# read this and pass --duration, so they clear their own surface instead of
# being hard-killed mid-render.
export YLESS_DURATION

mkdir -p "$(dirname "$PROGRESS")"
: > "$PROGRESS"

# Collect demos: every *.sh under demo/scripts except this driver and sourced
# helper libs (_*.sh). yctl/* needs an external RPC server, so it is opt-in.
collected=()
while IFS= read -r demo; do
    case "$demo" in
        */yctl/*) [ "$RUN_YCTL" = 1 ] || continue ;;
    esac
    collected+=("$demo")
done < <(find "$DIR" -name '*.sh' ! -name 'all.sh' ! -name '_*.sh' | sort)

total=${#collected[@]}
grand=$((total * REPEAT))

echo "all.sh: $total demos x ${REPEAT} = $grand runs, cap=${CAP}s, build=$BUILD"

if [ "$DRYRUN" = 1 ]; then
    for demo in "${collected[@]}"; do
        printf '  %s\n' "${demo#$ROOT/}"
    done
    exit 0
fi

index=0
for rep in $(seq 1 "$REPEAT"); do
    for demo in "${collected[@]}"; do
        index=$((index + 1))
        rel="${demo#$ROOT/}"
        printf '[%s] %d/%d rep%d %s\n' "$(date +%H:%M:%S)" "$index" "$grand" "$rep" "$rel" >> "$PROGRESS"
        printf '\n===== [%d/%d] %s =====\n' "$index" "$grand" "$rel"
        # </dev/null so pagers/interactive viewers get EOF and exit fast; the
        # timeout is the hard backstop. Never let one demo abort the run.
        #
        # yless is the exception: its figure-attach handshake reads the RPC
        # reply from stdin, so it needs the inherited PTY there, not /dev/null.
        # It self-exits via --duration (YLESS_DURATION, exported above) instead
        # of relying on an EOF.
        case "$demo" in
            */yless/*) timeout -k 2 "$CAP" bash "$demo" || true ;;
            *)         timeout -k 2 "$CAP" bash "$demo" </dev/null || true ;;
        esac
        # A killed raw-mode pager can leave the line discipline dirty; reset.
        stty sane 2>/dev/null || true
    done
done

printf '\nall.sh: done — %d demo runs\n' "$index"
echo "DONE" >> "$PROGRESS"
