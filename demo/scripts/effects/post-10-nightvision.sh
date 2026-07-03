#!/usr/bin/env bash
# Night vision — amplified green + noise
# yfx_post effect, index 10. Run inside a yetty terminal:
#   ./post-10-nightvision.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Night vision — amplified green + noise"
yfx_post 10 0.9 0.15 0.6
sleep "${1:-6}"
yfx_off
