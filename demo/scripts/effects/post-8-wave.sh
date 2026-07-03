#!/usr/bin/env bash
# Wave — brightness ripple
# yfx_post effect, index 8. Run inside a yetty terminal:
#   ./post-8-wave.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Wave — brightness ripple"
yfx_post 8 0.3 0.05 2
sleep "${1:-6}"
yfx_off
