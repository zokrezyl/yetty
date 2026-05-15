#!/bin/bash
# YPaint Scrolling Layer Demo: Simple inline graphic
# A small graphic that scrolls with terminal output (OSC 666675)

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../../.."

YAML_PAYLOAD=$(cat <<'EOF'
# Simple inline graphic for scrolling layer
background: "#00000000"

body:
  # Small colored circles in a row
  - circle:
      position: [20, 20]
      radius: 12
      fill: "#e74c3c"

  - circle:
      position: [50, 20]
      radius: 12
      fill: "#f39c12"

  - circle:
      position: [80, 20]
      radius: 12
      fill: "#2ecc71"

  - circle:
      position: [110, 20]
      radius: 12
      fill: "#3498db"

  - circle:
      position: [140, 20]
      radius: 12
      fill: "#9b59b6"

  - text:
      position: [170, 28]
      content: "Inline graphic"
      font-size: 18
      color: "#ecf0f1"
EOF
)

PAYLOAD=$(echo "$YAML_PAYLOAD" | base64 -w0)
printf '\033]600002;;%s\033\\' "$PAYLOAD"
echo ""
