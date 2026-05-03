# Shared bits for the yrpc-script wrappers. Source this from each demo.
# Picks up YRPC_HOST / YRPC_PORT from the environment so a user can point
# the demo at any running yetty instance:
#
#   YRPC_PORT=9999 ./demo/scripts/yrpc-scripts/hello.sh
#
# Default: 127.0.0.1:9999 (matches yrpc.py default).

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YRPC="${YRPC:-$ROOT/tools/yrpc-client/yrpc.py}"
HOST="${YRPC_HOST:-127.0.0.1}"
PORT="${YRPC_PORT:-9999}"

if [ ! -f "$YRPC" ]; then
    echo "yrpc.py not found at $YRPC — set YRPC=path/to/yrpc.py" >&2
    exit 1
fi

# Each wrapper sets ASSET to its yaml under demo/assets/yrpc-scripts/.
play_asset() {
    local asset="$1"
    if [ ! -f "$asset" ]; then
        echo "asset not found: $asset" >&2
        exit 1
    fi
    echo "==> playing $asset against $HOST:$PORT"
    uv run "$YRPC" --host "$HOST" --port "$PORT" plays "$asset"
}
