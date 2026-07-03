#!/usr/bin/env bash
# Drunk — woozy swaying
# yfx_coord effect, index 12. Run inside a yetty terminal:
#   ./coord-12-drunk.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Drunk — woozy swaying"
yfx_coord 12 0.4 0.5
sleep "${1:-6}"
yfx_off
