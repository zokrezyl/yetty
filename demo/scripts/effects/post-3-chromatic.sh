#!/usr/bin/env bash
# Chromatic aberration — RGB channel split
# yfx_post effect, index 3. Run inside a yetty terminal:
#   ./post-3-chromatic.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Chromatic aberration — RGB channel split"
yfx_post 3 0.8
sleep "${1:-6}"
yfx_off
