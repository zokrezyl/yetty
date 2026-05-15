#!/bin/bash
# YPaint Static Layer Demo: Geometric shapes (polygons and stars)
# Fixed overlay (OSC 666675)

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../../.."

YAML_PAYLOAD=$(cat <<'EOF'
background: "#00000000"

body:
  # Title
  - text:
      position: [50, 30]
      content: "Static Overlay - Geometric Shapes"
      font-size: 28
      color: "#ffffff"

  # Pentagon
  - pentagon:
      position: [100, 120]
      radius: 40
      fill: "#3498db"
      stroke: "#2980b9"
      stroke-width: 2

  - text:
      position: [70, 180]
      content: "Pentagon"
      font-size: 14
      color: "#ecf0f1"

  # Hexagon
  - hexagon:
      position: [220, 120]
      radius: 40
      fill: "#2ecc71"
      stroke: "#27ae60"
      stroke-width: 2

  - text:
      position: [193, 180]
      content: "Hexagon"
      font-size: 14
      color: "#ecf0f1"

  # Octogon
  - octogon:
      position: [340, 120]
      radius: 40
      fill: "#e74c3c"
      stroke: "#c0392b"
      stroke-width: 2

  - text:
      position: [310, 180]
      content: "Octagon"
      font-size: 14
      color: "#ecf0f1"

  # Equilateral Triangle
  - equilateral_triangle:
      position: [460, 120]
      radius: 40
      fill: "#f39c12"
      stroke: "#d68910"
      stroke-width: 2

  - text:
      position: [420, 180]
      content: "Triangle"
      font-size: 14
      color: "#ecf0f1"

  # 5-point Star
  - star:
      position: [100, 280]
      radius: 45
      points: 5
      inner: 2.5
      fill: "#f1c40f"
      stroke: "#d4ac0d"
      stroke-width: 2

  - text:
      position: [70, 350]
      content: "5-Star"
      font-size: 14
      color: "#ecf0f1"

  # 6-point Star
  - star:
      position: [220, 280]
      radius: 45
      points: 6
      inner: 2.0
      fill: "#9b59b6"
      stroke: "#8e44ad"
      stroke-width: 2

  - text:
      position: [190, 350]
      content: "6-Star"
      font-size: 14
      color: "#ecf0f1"

  # Hexagram
  - hexagram:
      position: [340, 280]
      radius: 45
      fill: "#1abc9c"
      stroke: "#16a085"
      stroke-width: 2

  - text:
      position: [303, 350]
      content: "Hexagram"
      font-size: 14
      color: "#ecf0f1"

  # Pentagram
  - pentagram:
      position: [460, 280]
      radius: 45
      fill: "#e91e63"
      stroke: "#c2185b"
      stroke-width: 2

  - text:
      position: [420, 350]
      content: "Pentagram"
      font-size: 14
      color: "#ecf0f1"
EOF
)

PAYLOAD=$(echo "$YAML_PAYLOAD" | base64 -w0)
printf '\033]600002;;%s\033\\' "$PAYLOAD"
echo "YPaint static layer: geometric shapes demo"
