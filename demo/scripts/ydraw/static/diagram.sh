#!/bin/bash
# YPaint Scrolling Layer Demo: Simple diagram
# A flowchart-style diagram that scrolls (OSC 666675)

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../../.."

YAML_PAYLOAD=$(cat <<'EOF'
# Simple flowchart diagram for scrolling layer
background: "#00000000"

body:
  # Title
  - text:
      position: [50, 20]
      content: "Process Flow"
      font-size: 22
      color: "#ffffff"

  # Start node
  - circle:
      position: [100, 70]
      radius: 30
      fill: "#2ecc71"
      stroke: "#27ae60"
      stroke-width: 2

  - text:
      position: [80, 78]
      content: "Start"
      font-size: 14
      color: "#ffffff"

  # Arrow 1
  - segment:
      from: [130, 70]
      to: [170, 70]
      stroke: "#7f8c8d"
      stroke-width: 2

  # Process 1
  - box:
      position: [220, 70]
      size: [100, 50]
      fill: "#3498db"
      stroke: "#2980b9"
      stroke-width: 2
      round: 6

  - text:
      position: [230, 78]
      content: "Process A"
      font-size: 14
      color: "#ffffff"

  # Arrow 2
  - segment:
      from: [320, 70]
      to: [360, 70]
      stroke: "#7f8c8d"
      stroke-width: 2

  # Process 2
  - box:
      position: [410, 70]
      size: [100, 50]
      fill: "#9b59b6"
      stroke: "#8e44ad"
      stroke-width: 2
      round: 6

  - text:
      position: [420, 78]
      content: "Process B"
      font-size: 14
      color: "#ffffff"

  # Arrow 3
  - segment:
      from: [510, 70]
      to: [550, 70]
      stroke: "#7f8c8d"
      stroke-width: 2

  # End node
  - circle:
      position: [590, 70]
      radius: 30
      fill: "#e74c3c"
      stroke: "#c0392b"
      stroke-width: 2

  - text:
      position: [575, 78]
      content: "End"
      font-size: 14
      color: "#ffffff"
EOF
)

PAYLOAD=$(echo "$YAML_PAYLOAD" | base64 -w0)
printf '\033]600002;;%s\033\\' "$PAYLOAD"
echo "YPaint scrolling layer: diagram demo"
