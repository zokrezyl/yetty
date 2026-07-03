#!/usr/bin/env bash
# Earthquake — violent shaking
# yfx_coord effect, index 15. Run inside a yetty terminal:
#   ./coord-15-earthquake.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Earthquake — violent shaking"
yfx_coord 15 0.5 30
sleep "${1:-6}"
yfx_off
