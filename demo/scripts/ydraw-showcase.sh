#!/bin/bash
# YDraw demo: 2D showcase with widgets
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/showcase.yaml -w 60 -H 25
