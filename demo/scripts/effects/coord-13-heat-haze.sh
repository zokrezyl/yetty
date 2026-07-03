#!/usr/bin/env bash
# Heat haze — rising shimmer
# yfx_coord effect, index 13. Run inside a yetty terminal:
#   ./coord-13-heat-haze.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Heat haze — rising shimmer"
yfx_coord 13 0.2 20 3
sleep "${1:-6}"
yfx_off
