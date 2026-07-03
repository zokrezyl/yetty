#!/usr/bin/env bash
# Magnify mouse — lens over the pointer
# yfx_coord effect, index 3. Run inside a yetty terminal:
#   ./coord-3-magnify-mouse.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Magnify mouse — lens over the pointer"
yfx_coord 3 0.7 150
sleep "${1:-6}"
yfx_off
