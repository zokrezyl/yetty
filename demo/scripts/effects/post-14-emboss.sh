#!/usr/bin/env bash
# Emboss — engraved relief look
# yfx_post effect, index 14. Run inside a yetty terminal:
#   ./post-14-emboss.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Emboss — engraved relief look"
yfx_post 14 1.0 0.785
sleep "${1:-6}"
yfx_off
