# Shared one-shot recording harness for the clip scenarios under
# demo/assets/yctl/clips/. Source this from each clip wrapper, prepare the
# clip's staging directory, then call:
#
#   record_clip <scenario.yaml> <staging-dir> <output-basename>.mp4
#
# Unlike the plain wrappers one level up (which drive an already-running
# yetty), record_clip launches a DEDICATED yetty instance — the clip
# scenarios end with a `shutdown` step that exits it and finalizes the
# --record MP4. Never point it at a yetty session you care about.
#
# The instance runs with <staging-dir> as its working directory so the
# scenario can type bare filenames (`ycat report.pdf`) with no build paths
# on screen, and with the build's ycat directory prepended to PATH for the
# same reason. All artifacts (staged inputs, yetty-record.log, the MP4)
# stay in the staging directory.
#
# Environment knobs (in addition to YCTL_HOST/YCTL_PORT from ../_common.sh):
#   YETTY_BUILD_DIR  build tree with yetty + tools
#                    (default: <repo>/build-desktop-ytrace-release)

CLIPS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$CLIPS_DIR/../_common.sh"

BUILD_DIR="${YETTY_BUILD_DIR:-$ROOT/build-desktop-ytrace-release}"
YETTY_BIN="$BUILD_DIR/yetty"
TOOL_PATH="$BUILD_DIR/tools/ycat:$BUILD_DIR/tools/ydiagram:$BUILD_DIR/tools/yplot:$BUILD_DIR/tools/yplot-stream:$BUILD_DIR/tools/ymath:$BUILD_DIR/tools/ymap:$BUILD_DIR/tools/ychart:$BUILD_DIR/tools/yvideo:$BUILD_DIR/tools/yflame:$BUILD_DIR/tools/ymesh:$BUILD_DIR/tools/ythorvg:$BUILD_DIR/tools/yecho:$BUILD_DIR/demo/yrdawn/12_nbody"

# True once something accepts TCP connections on $HOST:$PORT. The
# "server listening" log line is trace-level (invisible at the default
# log floor), so probing the port is the only reliable readiness signal.
rpc_server_ready() {
    (exec 3<>"/dev/tcp/$HOST/$PORT") 2>/dev/null
}

record_clip() {
    local scenario="$1"
    local staging_dir="$2"
    local output_name="$3"
    # Optional working directory for the recording session. Defaults to the
    # staging dir (the classic staged-assets clips); scenario-driven clips
    # that type real repo paths pass the repo root and stage nothing.
    local run_cwd="${4:-$staging_dir}"
    local log_file="$staging_dir/yetty-record.log"

    if [ ! -x "$YETTY_BIN" ]; then
        echo "yetty binary not found at $YETTY_BIN" >&2
        echo "build it first: USE_DISTCC=1 make build-desktop-ytrace-release" >&2
        exit 1
    fi
    if [ ! -f "$scenario" ]; then
        echo "scenario not found: $scenario" >&2
        exit 1
    fi
    mkdir -p "$staging_dir"

    (
        cd "$run_cwd"
        exec env PATH="$TOOL_PATH:$PATH" "$YETTY_BIN" \
            --record "$staging_dir/$output_name" --rpc-port "$PORT" -e bash
    ) > "$log_file" 2>&1 &
    local yetty_pid=$!

    # From here on, any early exit must take the recording instance down
    # with it — but only OUR pid, never by name or pattern.
    trap "kill $yetty_pid 2>/dev/null || true" EXIT

    # Wait for the RPC server to accept connections before playing (up
    # to 10s).
    local attempt
    for attempt in $(seq 1 50); do
        if rpc_server_ready; then
            break
        fi
        if ! kill -0 "$yetty_pid" 2>/dev/null; then
            echo "yetty exited before the RPC server came up; log tail:" >&2
            tail -20 "$log_file" >&2
            exit 1
        fi
        sleep 0.2
    done
    if ! rpc_server_ready; then
        echo "RPC server not reachable on $HOST:$PORT (port in use by" \
             "something else? try YCTL_PORT=<free port>); log tail:" >&2
        tail -20 "$log_file" >&2
        exit 1
    fi

    echo "==> recording $output_name (yetty pid $yetty_pid, port $PORT)"
    play_asset "$scenario"

    # The scenario's final `shutdown` step exits yetty, which finalizes
    # the MP4. Give it a grace period (20s), then fall back to killing
    # our own pid so a wedged run still terminates.
    for attempt in $(seq 1 100); do
        if ! kill -0 "$yetty_pid" 2>/dev/null; then
            break
        fi
        sleep 0.2
    done
    if kill -0 "$yetty_pid" 2>/dev/null; then
        echo "yetty (pid $yetty_pid) still running after the scenario;" \
             "killing it — the MP4 may be truncated" >&2
        kill "$yetty_pid" 2>/dev/null || true
    fi
    wait "$yetty_pid" 2>/dev/null || true
    trap - EXIT

    local output_path="$staging_dir/$output_name"
    if [ ! -s "$output_path" ]; then
        echo "recording failed: $output_path missing or empty; log tail:" >&2
        tail -20 "$log_file" >&2
        exit 1
    fi
    echo "==> clip ready: $output_path"
}
