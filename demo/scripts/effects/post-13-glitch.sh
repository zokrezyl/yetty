#!/usr/bin/env bash
# Glitch — bursty slice/channel corruption
# yfx_post effect, index 13. Run inside a yetty terminal:
#   ./post-13-glitch.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Glitch — bursty slice/channel corruption"
yfx_post 13 0.6 20 5
sleep "${1:-6}"
yfx_off
