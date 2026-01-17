#!/bin/bash
# YDraw Plugin: All 2D primitives - complete SDF shape collection
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../.."
uv run python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/all-primitives.yaml -w 40 -H 15
