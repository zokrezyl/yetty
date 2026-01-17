#!/bin/bash
# YDraw Plugin: 2D showcase - circles, rectangles, lines with GPU SDF rendering
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../.."
uv run python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/showcase.yaml -w 60 -H 25
