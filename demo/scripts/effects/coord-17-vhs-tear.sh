#!/usr/bin/env bash
# VHS tear — a torn tracking band
# yfx_coord effect, index 17. Run inside a yetty terminal:
#   ./coord-17-vhs-tear.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> VHS tear — a torn tracking band"
yfx_coord 17 0.6 50 2
sleep "${1:-6}"
yfx_off
