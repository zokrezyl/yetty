#!/usr/bin/env bash
# Bulge lens — magnifies under the mouse (follows the pointer)
# yfx_coord effect, index 8. Run inside a yetty terminal:
#   ./coord-8-bulge.sh [hold-seconds]   — then move the mouse over the pane
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Bulge lens — magnifies under the mouse (follows the pointer)"
yfx_coord 8 0.6 220
sleep "${1:-8}"
yfx_off
