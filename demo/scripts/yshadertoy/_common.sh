# Shared harness for the yshadertoy demos. Source this from each demo script.
#
# Each demo drives a running yetty over yctl RPC: it types a
# `tools/yshadertoy/yshadertoy.py <id>` line into the target instance, waits
# for the shader to fetch + convert + draw, then asks yetty to capture the
# real GPU frame with the `screenshot` RPC (not an X11 grab). The PPM is
# converted to PNG when ImageMagick's `convert` is present.
#
# Two ways to use every demo script:
#
#   1. Drive a yetty you already have open (recommended). Start one in a
#      separate terminal, NOT the one you run the demo from:
#        ./build-desktop-ytrace-release/yetty --rpc-port=9999 -e bash
#      then, from another terminal:
#        ./demo/scripts/yshadertoy/01-creation.sh
#
#   2. Let the demo launch its own throwaway yetty. If nothing is listening
#      on the RPC port the harness starts a dedicated instance, drives it,
#      screenshots, and shuts it down again.
#
# Environment knobs:
#   YCTL_HOST / YCTL_PORT  target instance          (default 127.0.0.1:9999)
#   YETTY_BUILD_DIR        build tree with yetty+ycat (default build-desktop-ytrace-release)
#   TINT                   path to the tint CLI      (from a dawn-exotic release;
#                                                      required for the WGSL step)
#   YSHADERTOY_SETTLE      seconds to wait for fetch+convert+draw before the shot
#                                                     (default 30)
#   YSHADERTOY_SHOTDIR     where screenshots land     (default tmp/yshadertoy-demo/shots)

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCTL="${YCTL:-$ROOT/tools/yctl-client/yctl.py}"
HOST="${YCTL_HOST:-127.0.0.1}"
PORT="${YCTL_PORT:-9999}"
BUILD_DIR="${YETTY_BUILD_DIR:-$ROOT/build-desktop-ytrace-release}"
YETTY_BIN="$BUILD_DIR/yetty"
YCAT_DIR="$BUILD_DIR/tools/ycat"
TOOL="$ROOT/tools/yshadertoy/yshadertoy.py"
SETTLE="${YSHADERTOY_SETTLE:-30}"
SHOTDIR="${YSHADERTOY_SHOTDIR:-$ROOT/tmp/yshadertoy-demo/shots}"

if [ ! -f "$YCTL" ]; then
    echo "yctl.py not found at $YCTL — set YCTL=path/to/yctl.py" >&2
    exit 1
fi
if [ ! -f "$TOOL" ]; then
    echo "yshadertoy tool not found at $TOOL" >&2
    exit 1
fi

ST_OWN_YETTY=0
ST_YETTY_PID=""

# True once something accepts TCP connections on $HOST:$PORT.
st_rpc_ready() { (exec 3<>"/dev/tcp/$HOST/$PORT") 2>/dev/null; }

# Refuse to run from inside the very yetty we would drive: the injected
# keystrokes would land in this busy shell and only replay after we exit.
st_refuse_self_drive() {
    case "$HOST" in 127.0.0.1 | localhost | ::1) ;; *) return 0 ;; esac
    local listener ancestor
    listener=$(ss -ltnp 2>/dev/null | grep -F ":$PORT " | grep -oP 'pid=\K[0-9]+' | head -1)
    [ -z "$listener" ] && return 0
    ancestor=$$
    while [ -n "$ancestor" ] && [ "$ancestor" -gt 1 ] 2>/dev/null; do
        if [ "$ancestor" = "$listener" ]; then
            echo "error: this terminal IS the yetty listening on port $PORT." >&2
            echo "Run the demo from a different terminal, or target another" >&2
            echo "instance with YCTL_PORT=<port>." >&2
            exit 1
        fi
        ancestor=$(ps -o ppid= -p "$ancestor" 2>/dev/null | tr -d ' ')
    done
}

# Reuse a running instance if the port is open; otherwise launch a dedicated
# throwaway yetty and remember to shut only that one down.
st_ensure_yetty() {
    if st_rpc_ready; then
        st_refuse_self_drive
        echo "==> driving the yetty already listening on $HOST:$PORT"
        return 0
    fi
    if [ ! -x "$YETTY_BIN" ]; then
        echo "no yetty on $HOST:$PORT and no binary at $YETTY_BIN to launch." >&2
        echo "build it: USE_DISTCC=1 make build-desktop-ytrace-release" >&2
        exit 1
    fi
    echo "==> nothing on $HOST:$PORT — launching a throwaway yetty"
    mkdir -p "$ROOT/tmp/yshadertoy-demo"
    (
        cd "$ROOT"
        exec env PATH="$YCAT_DIR:$PATH" "$YETTY_BIN" --rpc-port "$PORT" -e bash
    ) > "$ROOT/tmp/yshadertoy-demo/yetty.log" 2>&1 &
    ST_YETTY_PID=$!
    ST_OWN_YETTY=1
    trap 'st_teardown' EXIT
    local attempt
    for attempt in $(seq 1 60); do
        st_rpc_ready && break
        if ! kill -0 "$ST_YETTY_PID" 2>/dev/null; then
            echo "yetty exited before its RPC server came up; log tail:" >&2
            tail -20 "$ROOT/tmp/yshadertoy-demo/yetty.log" >&2
            exit 1
        fi
        sleep 0.5
    done
    st_rpc_ready || { echo "RPC never came up on $HOST:$PORT" >&2; exit 1; }
    sleep 1
}

st_run() { uv run "$YCTL" --host "$HOST" --port "$PORT" run "$1"; }

# Capture the current GPU frame via yetty's own screenshot RPC. $1 = abs .ppm
# path. Converts to a sibling .png when `convert` is available.
st_shot() {
    local ppm="$1"
    mkdir -p "$(dirname "$ppm")"
    uv run "$YCTL" --host "$HOST" --port "$PORT" screenshot "$ppm"
    # the capture is async inside yetty; give the readback a moment to flush
    sleep 2
    if [ -s "$ppm" ] && command -v convert >/dev/null 2>&1; then
        convert "$ppm" "${ppm%.ppm}.png" 2>/dev/null && rm -f "$ppm"
        echo "==> ${ppm%.ppm}.png"
    else
        echo "==> $ppm"
    fi
}

# Drive one shader end to end and screenshot it. $1 = shader id, $2 = title.
# Waits for the tool's manifest.json (conversion done, ycat about to draw)
# rather than blind-sleeping, capping the wait at YSHADERTOY_SETTLE seconds.
st_demo() {
    local id="$1" title="${2:-$1}"
    local outdir="$ROOT/tmp/yshadertoy-demo/out-$id"
    echo "==> $id  $title"
    rm -f "$outdir/manifest.json" 2>/dev/null
    st_run "clear"
    sleep 1
    st_run "echo '--- yshadertoy: $title ($id) ---'; $TOOL $id -o tmp/yshadertoy-demo/out-$id"
    local attempt
    for attempt in $(seq 1 "$SETTLE"); do
        [ -s "$outdir/manifest.json" ] && break
        sleep 1
    done
    if [ -s "$outdir/manifest.json" ]; then
        sleep 4                       # let ycat draw the figure after convert
    else
        echo "   (no manifest after ${SETTLE}s — screenshotting anyway)" >&2
    fi
    st_shot "$SHOTDIR/$id.ppm"
}

st_teardown() {
    trap - EXIT
    if [ "$ST_OWN_YETTY" = "1" ] && [ -n "$ST_YETTY_PID" ]; then
        echo "==> shutting down throwaway yetty (pid $ST_YETTY_PID)"
        uv run "$YCTL" --host "$HOST" --port "$PORT" shutdown >/dev/null 2>&1 || true
        local attempt
        for attempt in $(seq 1 50); do
            kill -0 "$ST_YETTY_PID" 2>/dev/null || break
            sleep 0.2
        done
        kill "$ST_YETTY_PID" 2>/dev/null || true
        wait "$ST_YETTY_PID" 2>/dev/null || true
    fi
}
