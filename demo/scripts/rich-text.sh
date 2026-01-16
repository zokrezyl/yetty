#!/bin/bash
# Rich Text Plugin Demo

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/../../tools/yetty-client"
DEMO_FILE="$SCRIPT_DIR/../assets/rich-text/simple.yaml"

cd "$CLIENT_DIR" && uv run python main.py create rich-text -x 2 -y 2 -w 76 -H 30 -i "$DEMO_FILE"
