#!/bin/bash
# YPaint Scrolling Layer: Single primitive for scroll testing

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/../../../.."

YAML_PAYLOAD=$(cat <<'EOF'
background: "#00000000"

body:
  - circle:
      position: [100, 70]
      radius: 30
      fill: "#2ecc71"
      stroke: "#27ae60"
      stroke-width: 2
EOF
)

PAYLOAD=$(echo "$YAML_PAYLOAD" | base64 -w0)
printf '\033]600002;;%s\033\\' "$PAYLOAD"
echo "one-primitive test"
