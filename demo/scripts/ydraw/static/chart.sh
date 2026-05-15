#!/bin/bash
# YPaint Scrolling Layer Demo: Simple bar chart
# A chart that scrolls with terminal output (OSC 666675)

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../../.."

YAML_PAYLOAD=$(cat <<'EOF'
# Simple bar chart for scrolling layer
background: "#00000000"

body:
  # Chart title
  - text:
      position: [50, 25]
      content: "Monthly Sales"
      font-size: 20
      color: "#ffffff"

  # Y-axis
  - segment:
      from: [50, 40]
      to: [50, 180]
      stroke: "#7f8c8d"
      stroke-width: 2

  # X-axis
  - segment:
      from: [50, 180]
      to: [350, 180]
      stroke: "#7f8c8d"
      stroke-width: 2

  # Bars
  - box:
      position: [70, 100]
      size: [40, 80]
      fill: "#3498db"
      round: 4

  - box:
      position: [120, 60]
      size: [40, 120]
      fill: "#2ecc71"
      round: 4

  - box:
      position: [170, 80]
      size: [40, 100]
      fill: "#e74c3c"
      round: 4

  - box:
      position: [220, 120]
      size: [40, 60]
      fill: "#f39c12"
      round: 4

  - box:
      position: [270, 50]
      size: [40, 130]
      fill: "#9b59b6"
      round: 4

  # Labels
  - text:
      position: [75, 195]
      content: "Jan"
      font-size: 12
      color: "#bdc3c7"

  - text:
      position: [125, 195]
      content: "Feb"
      font-size: 12
      color: "#bdc3c7"

  - text:
      position: [175, 195]
      content: "Mar"
      font-size: 12
      color: "#bdc3c7"

  - text:
      position: [225, 195]
      content: "Apr"
      font-size: 12
      color: "#bdc3c7"

  - text:
      position: [275, 195]
      content: "May"
      font-size: 12
      color: "#bdc3c7"
EOF
)

PAYLOAD=$(echo "$YAML_PAYLOAD" | base64 -w0)
printf '\033]600002;;%s\033\\' "$PAYLOAD"
echo "YPaint scrolling layer: bar chart demo"
