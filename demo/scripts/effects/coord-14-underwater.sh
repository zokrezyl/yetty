#!/usr/bin/env bash
# Underwater — rolling caustic waves
# yfx_coord effect, index 14. Run inside a yetty terminal:
#   ./coord-14-underwater.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Underwater — rolling caustic waves"
yfx_coord 14 0.3 4 1.5
sleep "${1:-6}"
yfx_off
