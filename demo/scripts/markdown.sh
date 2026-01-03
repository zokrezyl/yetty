#!/bin/bash
# Markdown Plugin Demo for yetty terminal
#
# Usage: Run this script inside yetty terminal
#
# Requires: yetty-client (tools/yetty-client/main.py)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YETTY_CLIENT="$SCRIPT_DIR/../../tools/yetty-client/main.py"

# Check if yetty-client exists
if [[ ! -f "$YETTY_CLIENT" ]]; then
    echo "Error: yetty-client not found at $YETTY_CLIENT"
    exit 1
fi

# Demo markdown content
MARKDOWN_CONTENT='# Markdown Plugin Demo

Welcome to the **yetty** markdown plugin! This demonstrates all supported features.

## Headers

Headers are created with `#` symbols. The more `#` symbols, the smaller the header.

### Level 3 Header

#### Level 4 Header

## Text Styles

- **Bold text** is wrapped in double asterisks
- *Italic text* is wrapped in single asterisks
- ***Bold and italic*** uses triple asterisks
- `Inline code` uses backticks

## Lists

Bullet lists use `-` or `*`:

- First item
- Second item with **bold**
- Third item with *italic*
- Fourth with `code`

## Code Examples

Here's some inline code: `printf("Hello, World!")`

And more code: `fn main() { println!("Rust!"); }`

## Mixed Styles

This paragraph has **bold words**, *italic words*, and `code snippets` all mixed together. You can even combine them like ***bold and italic*** text.

## Features

- MSDF font rendering for crisp text at any size
- Support for Bold, Italic, and BoldItalic font variants
- Word wrapping at layer boundaries
- Mouse wheel scrolling for long documents
- Header scaling (h1=1.6x, h2=1.45x, h3=1.3x, etc.)

---

*Thank you for trying the markdown plugin!*'

echo "Markdown Plugin Demo"
echo "===================="
echo ""
echo "Creating markdown layer at position (2, 3) with size 76x35..."
echo ""

# Run yetty-client to create the markdown layer
python3 "$YETTY_CLIENT" run markdown -M A -x 2 -y 3 -w 76 -H 35 -I "$MARKDOWN_CONTENT"

echo ""
echo "Layer created! Use mouse wheel to scroll the content."
echo ""
echo "To list active layers:"
echo "  python3 $YETTY_CLIENT ls"
echo ""
