#!/usr/bin/env bash
# Rain — falling water streaks
# yfx_post effect, index 15. Run inside a yetty terminal:
#   ./post-15-rain.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Rain — falling water streaks"
yfx_post 15 0.4 3 0.15
sleep "${1:-6}"
yfx_off
