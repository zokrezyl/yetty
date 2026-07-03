#!/usr/bin/env bash
# Heartbeat — rhythmic pulse from centre
# yfx_coord effect, index 11. Run inside a yetty terminal:
#   ./coord-11-heartbeat.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Heartbeat — rhythmic pulse from centre"
yfx_coord 11 0.3 72
sleep "${1:-6}"
yfx_off
