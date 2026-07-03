#!/usr/bin/env bash
# Broken TV — rolling band, static, glitch lines
# yfx_post effect, index 4. Run inside a yetty terminal:
#   ./post-4-broken-tv.sh [hold-seconds]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../effects.sh"

yfx_fill
echo; echo ">>> Broken TV — rolling band, static, glitch lines"
yfx_post 4 0.5 0.3 1.0
sleep "${1:-6}"
yfx_off
