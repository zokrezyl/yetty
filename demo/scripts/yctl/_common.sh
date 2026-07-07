# Shared bits for the yctl-script wrappers. Source this from each demo.
# Picks up YCTL_HOST / YCTL_PORT from the environment so a user can point
# the demo at any running yetty instance:
#
#   YCTL_PORT=9999 ./demo/scripts/yctl/hello.sh
#
# Default: 127.0.0.1:9999 (matches yctl.py default).
#
# IMPORTANT: run these wrappers from a terminal that is NOT the yetty
# instance being driven. The script injects keystrokes into the target's
# PTY; if the target terminal is the one running this script, the shell
# there is busy running the script and cannot read the injected input —
# the typed text just piles up on screen unexecuted, and the moment the
# script exits the shell reads it all back and executes every command in
# one batch. refuse_self_drive() below catches the common case.

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCTL="${YCTL:-$ROOT/tools/yctl-client/yctl.py}"
HOST="${YCTL_HOST:-127.0.0.1}"
PORT="${YCTL_PORT:-9999}"

if [ ! -f "$YCTL" ]; then
    echo "yctl.py not found at $YCTL — set YCTL=path/to/yctl.py" >&2
    exit 1
fi

# Abort when this script is running inside the very yetty it is about to
# drive: the target instance's pid would be an ancestor of this shell.
# Only meaningful for local targets; remote hosts cannot be our terminal.
refuse_self_drive() {
    case "$HOST" in
    127.0.0.1 | localhost | ::1) ;;
    *) return 0 ;;
    esac

    local listener_pid
    listener_pid=$(ss -ltnp 2>/dev/null | grep -F ":$PORT " | grep -oP 'pid=\K[0-9]+' | head -1)
    if [ -z "$listener_pid" ]; then
        return 0
    fi

    local ancestor=$$
    while [ -n "$ancestor" ] && [ "$ancestor" -gt 1 ] 2>/dev/null; do
        if [ "$ancestor" = "$listener_pid" ]; then
            echo "error: this terminal IS the yetty instance listening on port $PORT." >&2
            echo "The injected keystrokes would land in this busy shell and only execute" >&2
            echo "in one batch after the script exits. Run the demo from a different" >&2
            echo "terminal, or target another instance with YCTL_PORT=<port>." >&2
            exit 1
        fi
        ancestor=$(ps -o ppid= -p "$ancestor" 2>/dev/null | tr -d ' ')
    done
    return 0
}

# Each wrapper sets ASSET to its yaml under demo/assets/yctl/.
play_asset() {
    local asset="$1"
    if [ ! -f "$asset" ]; then
        echo "asset not found: $asset" >&2
        exit 1
    fi
    refuse_self_drive
    echo "==> playing $asset against $HOST:$PORT"
    uv run "$YCTL" --host "$HOST" --port "$PORT" plays "$asset"
}
