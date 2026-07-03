#!/usr/bin/env bash
# Thermal — false-colour heat ramp
# yfx_post effect, index 12. Run inside a yetty terminal:
#   ./post-12-thermal.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Thermal — false-colour heat ramp"
yfx_post 12 1.0
sleep "${1:-6}"
yfx_off
