#!/bin/bash
# Shader Plugin: Sinusoidal warp - texture deformation effect by Shane
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/../../../tools/yetty-client"
SHADER_FILE="$SCRIPT_DIR/../../assets/shader/sinusoidal-warp.wgsl"
TEXTURE_FILE="$SCRIPT_DIR/../../../docs/logo.jpeg"

cd "$CLIENT_DIR" && uv run python main.py create shadertoy -i "$SHADER_FILE" --channel0="$TEXTURE_FILE" -w 60 -H 30
