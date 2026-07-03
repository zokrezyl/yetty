#!/usr/bin/env bash
# Barrel — lens barrel distortion
# yfx_coord effect, index 6. Run inside a yetty terminal:
#   ./coord-6-barrel.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Barrel — lens barrel distortion"
yfx_coord 6 0.4
sleep "${1:-6}"
yfx_off
