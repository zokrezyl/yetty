#!/bin/bash
# Shader Plugin: Volumetric clouds - raymarched 3D cloud rendering by iq
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/../../../tools/yetty-client"
SHADER_FILE="$SCRIPT_DIR/../../assets/shader/clouds.wgsl"

cd "$CLIENT_DIR" && uv run python main.py create shadertoy -i "$SHADER_FILE" -w 60 -H 30
