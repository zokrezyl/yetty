#!/bin/bash
# ThorVG demo: Display YAML-defined vector graphics
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
uv run python3 tools/yetty-client/main.py create thorvg --yaml demo/assets/thorvg/shapes.yaml -w 60 -H 30
