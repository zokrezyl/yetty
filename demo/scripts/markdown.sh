#!/bin/bash
# Markdown Plugin Demo for yetty terminal
#
# Usage: Run this script inside yetty terminal

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."

MARKDOWN_FILE="README.md"

if [[ ! -f "$MARKDOWN_FILE" ]]; then
    echo "Error: Markdown file not found at $MARKDOWN_FILE"
    exit 1
fi

uv run python3 tools/yetty-client/main.py create markdown -x 2 -y 2 -w 76 -H 35 -i "$MARKDOWN_FILE"
