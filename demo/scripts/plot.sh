#!/bin/bash
# Plot Plugin: Interactive multi-line chart with sine waves, pan/zoom support
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
uv run python3 tools/yetty-client/main.py create plot -g sine -n 4 -m 200 -w 60 -H 30
