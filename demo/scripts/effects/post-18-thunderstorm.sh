#!/usr/bin/env bash
# Thunderstorm — lightning flashes + darken
# yfx_post effect, index 18. Run inside a yetty terminal:
#   ./post-18-thunderstorm.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Thunderstorm — lightning flashes + darken"
yfx_post 18 0.7 5 0.4
sleep "${1:-6}"
yfx_off
