#!/usr/bin/env bash
# Funhouse — mirror-maze bulges
# yfx_coord effect, index 20. Run inside a yetty terminal:
#   ./coord-20-funhouse.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Funhouse — mirror-maze bulges"
yfx_coord 20 0.4 3 2
sleep "${1:-6}"
yfx_off
