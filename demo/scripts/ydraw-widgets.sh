#!/bin/bash
# YDraw demo: Widget system example
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
python3 tools/yetty-client/main.py create ydraw -f demo/files/sdf/widgets-example.yaml -w 40 -H 20
