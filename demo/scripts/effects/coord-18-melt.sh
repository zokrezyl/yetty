#!/usr/bin/env bash
# Melt — columns dripping downward
# yfx_coord effect, index 18. Run inside a yetty terminal:
#   ./coord-18-melt.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Melt — columns dripping downward"
yfx_coord 18 0.5 8 1
sleep "${1:-6}"
yfx_off
