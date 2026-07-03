#!/usr/bin/env bash
# Pinch lens — squeezes under the mouse (follows the pointer)
# yfx_coord effect, index 9. Run inside a yetty terminal:
#   ./coord-9-pinch.sh [hold-seconds]   — then move the mouse over the pane
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Pinch lens — squeezes under the mouse (follows the pointer)"
yfx_coord 9 0.6 220
sleep "${1:-8}"
yfx_off
