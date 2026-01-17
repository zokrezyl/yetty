#!/bin/bash
# YDraw Plugin: 3D primitives - spheres, boxes, cylinders with raymarching
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../.."
uv run python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/3d-primitives.yaml -w 50 -H 30
