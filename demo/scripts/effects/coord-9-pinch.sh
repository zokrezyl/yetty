#!/usr/bin/env bash
# Pinch — squeeze toward the centre
# yfx_coord effect, index 9. Run inside a yetty terminal:
#   ./coord-9-pinch.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Pinch — squeeze toward the centre"
yfx_coord 9 0.5
sleep "${1:-6}"
yfx_off
