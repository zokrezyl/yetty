#!/usr/bin/env bash
# Scanline offset — per-row horizontal jitter
# yfx_coord effect, index 16. Run inside a yetty terminal:
#   ./coord-16-scanline-offset.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Scanline offset — per-row horizontal jitter"
yfx_coord 16 0.5 5
sleep "${1:-6}"
yfx_off
