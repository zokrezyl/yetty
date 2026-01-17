#!/bin/bash
# PDFium Plugin: Renders PDF documents with page navigation and zoom
# Uses PDFium library (BSD-3-Clause license) - MIT compatible alternative to MuPDF
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../.."

PDF_FILE="demo/assets/sample-local-pdf.pdf"

if [[ ! -f "$PDF_FILE" ]]; then
    echo "Error: PDF file not found at $PDF_FILE"
    exit 1
fi

uv run python3 tools/yetty-client/main.py create pdfium -x 2 -y 2 -w 76 -H 35 "$PDF_FILE"
