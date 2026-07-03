#!/usr/bin/env bash
# Swirl — animated rotational warp
# yfx_coord effect, index 7. Run inside a yetty terminal:
#   ./coord-7-swirl.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Swirl — animated rotational warp"
yfx_coord 7 3.0
sleep "${1:-6}"
yfx_off
