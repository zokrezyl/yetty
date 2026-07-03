#!/usr/bin/env bash
# Magnify cursor — lens over the terminal cursor
# yfx_coord effect, index 2. Run inside a yetty terminal:
#   ./coord-2-magnify-cursor.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Magnify cursor — lens over the terminal cursor"
yfx_coord 2 0.7 200
sleep "${1:-6}"
yfx_off
