#!/usr/bin/env bash
# Twist — spiral around the centre
# yfx_coord effect, index 21. Run inside a yetty terminal:
#   ./coord-21-twist.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Twist — spiral around the centre"
yfx_coord 21 0.5 0.3
sleep "${1:-6}"
yfx_off
