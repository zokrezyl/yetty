#!/usr/bin/env bash
# Bulge — magnify around the centre
# yfx_coord effect, index 8. Run inside a yetty terminal:
#   ./coord-8-bulge.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Bulge — magnify around the centre"
yfx_coord 8 0.5
sleep "${1:-6}"
yfx_off
