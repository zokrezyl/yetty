#!/bin/bash
# ThorVG Plugin: Long Lottie animation - 14 second confetti celebration
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../.."
uv run python3 tools/yetty-client/main.py create thorvg --lottie demo/files/thorvg/animation-long.json -w 40 -H 25
