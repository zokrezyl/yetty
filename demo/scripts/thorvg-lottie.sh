#!/bin/bash
# ThorVG demo: Display Lottie animation
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
uv run python3 tools/yetty-client/main.py create thorvg --lottie demo/files/thorvg/bouncing-ball.json -w 40 -H 25
