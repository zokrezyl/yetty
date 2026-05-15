#!/bin/bash
# YPaint Scrolling Layer Demo: Two plots
# Mathematical function plots that scroll with terminal output (OSC 666675)

YAML_PAYLOAD=$(cat <<'EOF'
background: "#00000000"

body:
  # First plot: sine and cosine
  - yplot:
      position: [10, 10]
      size: [300, 150]
      x_range: [-6.28, 6.28]
      y_range: [-1.5, 1.5]
      show_grid: true
      show_axes: true
      show_labels: true
      functions:
        - expr: "sin(x)"
          color: "#FF6B6B"
        - expr: "cos(x)"
          color: "#4ECDC4"

  # Second plot: parabola and line
  - yplot:
      position: [320, 10]
      size: [300, 150]
      x_range: [-5, 5]
      y_range: [-2, 10]
      show_grid: true
      show_axes: true
      show_labels: true
      functions:
        - expr: "x*x"
          color: "#FFE66D"
        - expr: "2*x + 1"
          color: "#AA96DA"
EOF
)

PAYLOAD=$(echo "$YAML_PAYLOAD" | base64 -w0)
printf '\033]600002;;%s\033\\' "$PAYLOAD"
echo ""
