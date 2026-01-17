#!/bin/bash
# YDraw Plugin: 3D showcase - advanced scene with lighting and shadows
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../.."
uv run python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/3d-showcase.yaml -w 50 -H 30
