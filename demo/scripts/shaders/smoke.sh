#!/bin/bash
# Demo: Smoke shader - fluid/smoke simulation with mouse interaction
# Move mouse to add smoke, watch it rise and diffuse

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/../../../tools/yetty-client"
SHADER_FILE="$SCRIPT_DIR/../../assets/shader/smoke.wgsl"

cd "$CLIENT_DIR" && uv run python main.py create shadertoy -i "$SHADER_FILE" -w 60 -H 30
