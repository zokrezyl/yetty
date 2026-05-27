# Shared bits for the yctl-script wrappers. Source this from each demo.
# Picks up YCTL_HOST / YCTL_PORT from the environment so a user can point
# the demo at any running yetty instance:
#
#   YCTL_PORT=9999 ./demo/scripts/yctl-scripts/hello.sh
#
# Default: 127.0.0.1:9999 (matches yctl.py default).

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCTL="${YCTL:-$ROOT/tools/yctl-client/yctl.py}"
HOST="${YCTL_HOST:-127.0.0.1}"
PORT="${YCTL_PORT:-9999}"

if [ ! -f "$YCTL" ]; then
    echo "yctl.py not found at $YCTL — set YCTL=path/to/yctl.py" >&2
    exit 1
fi

# Each wrapper sets ASSET to its yaml under demo/assets/yctl-scripts/.
play_asset() {
    local asset="$1"
    if [ ! -f "$asset" ]; then
        echo "asset not found: $asset" >&2
        exit 1
    fi
    echo "==> playing $asset against $HOST:$PORT"
    uv run "$YCTL" --host "$HOST" --port "$PORT" plays "$asset"
}
