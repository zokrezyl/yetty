#!/bin/bash
# Demo: Image plugin - display logo image
# Run this inside yetty terminal

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/../../tools/yetty-client"
YETTY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$CLIENT_DIR" && uv run python main.py run image -i "$YETTY_ROOT/docs/logo.jpeg" -w 40 -H 20
