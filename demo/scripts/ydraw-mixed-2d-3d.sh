#!/bin/bash
# YDraw demo: Mixed 2D and 3D primitives
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
uv run python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/mixed-2d-3d.yaml -w 50 -H 25
