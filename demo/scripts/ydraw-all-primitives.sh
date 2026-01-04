#!/bin/bash
# YDraw demo: All 2D primitives
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/all-primitives.yaml -w 40 -H 15
