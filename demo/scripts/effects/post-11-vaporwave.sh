#!/usr/bin/env bash
# Vaporwave — pink/cyan retro gradient
# yfx_post effect, index 11. Run inside a yetty terminal:
#   ./post-11-vaporwave.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Vaporwave — pink/cyan retro gradient"
yfx_post 11 0.5 0.5
sleep "${1:-6}"
yfx_off
