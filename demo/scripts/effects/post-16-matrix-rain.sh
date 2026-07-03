#!/usr/bin/env bash
# Matrix rain — cascading green code
# yfx_post effect, index 16. Run inside a yetty terminal:
#   ./post-16-matrix-rain.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Matrix rain — cascading green code"
yfx_post 16 0.6 1.5 15
sleep "${1:-6}"
yfx_off
