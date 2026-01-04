#!/bin/bash
# YDraw demo: 3D showcase - all 3D primitives
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
uv run python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/3d-showcase.yaml -w 50 -H 30
