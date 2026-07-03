#!/usr/bin/env bash
# Fisheye — bulge the centre outward
# yfx_coord effect, index 1. Run inside a yetty terminal:
#   ./coord-1-fisheye.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Fisheye — bulge the centre outward"
yfx_coord 1 0.6
sleep "${1:-6}"
yfx_off
