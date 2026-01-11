#!/bin/bash
# Demo: Paint shader - interactive painting with persistent strokes
# Click and drag to paint, use scroll wheel to change brush size

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/../../../tools/yetty-client"
SHADER_FILE="$SCRIPT_DIR/../../assets/shader/paint.wgsl"

cd "$CLIENT_DIR" && uv run python main.py create shadertoy -i "$SHADER_FILE" -w 60 -H 30
