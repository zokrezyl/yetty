#!/usr/bin/env bash
# Matrix — green tint with column shimmer
# yfx_post effect, index 5. Run inside a yetty terminal:
#   ./post-5-matrix.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Matrix — green tint with column shimmer"
yfx_post 5 0.8 2
sleep "${1:-6}"
yfx_off
