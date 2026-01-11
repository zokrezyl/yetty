#!/bin/bash
# Julia - Distance 1 - Julia set fractal by Inigo Quilez
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/../../../tools/yetty-client"
SHADER_FILE="$SCRIPT_DIR/../../assets/shader/julia-1.wgsl"

cd "$CLIENT_DIR" && uv run python main.py create shadertoy -i "$SHADER_FILE" -w 60 -H 30
