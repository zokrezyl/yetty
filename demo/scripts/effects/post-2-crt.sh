#!/usr/bin/env bash
# CRT monitor — vignette + scanlines
# yfx_post effect, index 2. Run inside a yetty terminal:
#   ./post-2-crt.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> CRT monitor — vignette + scanlines"
yfx_post 2 0.4 0.15 0.3
sleep "${1:-6}"
yfx_off
