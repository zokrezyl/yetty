#!/bin/bash
# ThorVG demo: Display SVG image
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
uv run python3 tools/yetty-client/main.py create thorvg --svg demo/files/thorvg/sunset.svg -w 50 -H 25
