#!/usr/bin/env bash
# Warts — pulsing blisters at random spots
# yfx_coord effect, index 4. Run inside a yetty terminal:
#   ./coord-4-warts.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Warts — pulsing blisters at random spots"
yfx_coord 4 0.4 5 80
sleep "${1:-6}"
yfx_off
