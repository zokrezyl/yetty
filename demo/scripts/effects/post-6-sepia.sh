#!/usr/bin/env bash
# Sepia — vintage warm monochrome
# yfx_post effect, index 6. Run inside a yetty terminal:
#   ./post-6-sepia.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Sepia — vintage warm monochrome"
yfx_post 6 0.8
sleep "${1:-6}"
yfx_off
