#!/usr/bin/env bash
# Wave — flag-like ripple
# yfx_coord effect, index 19. Run inside a yetty terminal:
#   ./coord-19-wave.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Wave — flag-like ripple"
yfx_coord 19 0.4 3 2
sleep "${1:-6}"
yfx_off
