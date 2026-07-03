#!/usr/bin/env bash
# Invert — colour inversion
# yfx_post effect, index 9. Run inside a yetty terminal:
#   ./post-9-invert.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Invert — colour inversion"
yfx_post 9 1.0
sleep "${1:-6}"
yfx_off
