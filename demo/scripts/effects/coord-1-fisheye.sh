#!/usr/bin/env bash
# Fisheye lens — a magnifying bubble that FOLLOWS THE MOUSE
# yfx_coord effect, index 1. Run inside a yetty terminal:
#   ./coord-1-fisheye.sh [hold-seconds]   — then move the mouse over the pane
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Fisheye lens — a magnifying bubble that FOLLOWS THE MOUSE"
yfx_coord 1 1.0 220
sleep "${1:-8}"
yfx_off
