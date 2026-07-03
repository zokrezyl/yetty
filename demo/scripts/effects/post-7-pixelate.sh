#!/usr/bin/env bash
# Pixelate — blocky posterized look
# yfx_post effect, index 7. Run inside a yetty terminal:
#   ./post-7-pixelate.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Pixelate — blocky posterized look"
yfx_post 7 6 6
sleep "${1:-6}"
yfx_off
