#!/bin/bash
# PDF Plugin Demo for yetty terminal
#
# Usage: Run this script inside yetty terminal

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."

PDF_FILE="demo/assets/sample-local-pdf.pdf"

if [[ ! -f "$PDF_FILE" ]]; then
    echo "Error: PDF file not found at $PDF_FILE"
    exit 1
fi

uv run python3 tools/yetty-client/main.py create pdf -x 2 -y 2 -w 76 -H 35 "$PDF_FILE"
