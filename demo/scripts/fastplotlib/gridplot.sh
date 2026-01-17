#!/bin/bash
# Python Plugin (fastplotlib): Grid layout with multiple subplots
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
cd "$ROOT_DIR"
~/.local/bin/uv run python tools/yetty-client/main.py create python \
    -i demo/assets/python/fastplotlib/gridplot.py -w 60 -H 30
