#!/bin/bash
# YDraw Plugin: UI widgets - buttons, sliders, progress bars with SDF
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../.."
uv run python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/widgets-example.yaml -w 40 -H 20
