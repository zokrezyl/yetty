#!/bin/bash
# Demo: Ymery plugin - ImGui interface
# Run this inside yetty terminal
# Requires: ymery-cpp demo layouts and yetty build with plugins

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/../../tools/yetty-client"
YETTY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Default paths - must be absolute!
LAYOUT_PATH="${YMERY_LAYOUTS:-$(cd "$YETTY_ROOT/../waew-meta/ymery-cpp/demo/layouts/editor" 2>/dev/null && pwd)}"
PLUGINS_PATH="${YMERY_PLUGINS:-$YETTY_ROOT/build/plugins}"

if [ -z "$LAYOUT_PATH" ] || [ ! -d "$LAYOUT_PATH" ]; then
    echo "Error: Layout path not found: $LAYOUT_PATH"
    echo "Set YMERY_LAYOUTS env var to your ymery layout directory"
    exit 1
fi

if [ ! -d "$PLUGINS_PATH" ]; then
    echo "Error: Plugins path not found: $PLUGINS_PATH"
    echo "Set YMERY_PLUGINS env var to your yetty build/plugins directory"
    exit 1
fi

cd "$CLIENT_DIR" && uv run python main.py run ymery -p "$LAYOUT_PATH" --plugins "$PLUGINS_PATH" -m app -w 40 -H 20
