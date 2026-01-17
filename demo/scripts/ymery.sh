#!/bin/bash
# Ymery Plugin: ImGui widgets rendered from YAML layout definitions
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."

# Use ymery-cpp demo layouts from the build directory
YMERY_LAYOUTS="build-desktop-release/_deps/ymery-cpp-src/demo/layouts"

uv run python3 tools/yetty-client/main.py create ymery \
    -p "$YMERY_LAYOUTS" \
    -m "$YMERY_LAYOUTS/simple/app.yaml" \
    -w 60 -H 30
