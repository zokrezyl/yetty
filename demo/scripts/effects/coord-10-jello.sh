#!/usr/bin/env bash
# Jello — wobbling gelatin wobble
# yfx_coord effect, index 10. Run inside a yetty terminal:
#   ./coord-10-jello.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Jello — wobbling gelatin wobble"
yfx_coord 10 0.3 3 2
sleep "${1:-6}"
yfx_off
