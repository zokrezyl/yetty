#!/usr/bin/env bash
# Scanlines — darken alternating rows
# yfx_post effect, index 1. Run inside a yetty terminal:
#   ./post-1-scanlines.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Scanlines — darken alternating rows"
yfx_post 1 0.4 2
sleep "${1:-6}"
yfx_off
