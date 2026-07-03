#!/usr/bin/env bash
# Wandering wart — a blister drifting around
# yfx_coord effect, index 5. Run inside a yetty terminal:
#   ./coord-5-wandering-wart.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Wandering wart — a blister drifting around"
yfx_coord 5 0.5 120 1
sleep "${1:-6}"
yfx_off
