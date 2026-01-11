#!/bin/bash
# ThorVG demo: Display long Lottie animation (14 seconds confetti)
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."
uv run python3 tools/yetty-client/main.py create thorvg --lottie demo/files/thorvg/confetti.json -w 40 -H 25
