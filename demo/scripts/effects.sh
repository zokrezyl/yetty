#!/usr/bin/env bash
# Shader effects demo + emitter for yetty.
#
# Effects are selected at runtime via OSC escape sequences, one per class:
#   ESC ] 666668 ; <base64("INDEX:P0:..:P5")> BEL   # post-color effect
#   ESC ] 666669 ; <base64("INDEX:P0:P1")>     BEL   # coordinate distortion
# INDEX 0 disables that class. Unlike the original proof-of-concept the body
# is base64-encoded, because yetty's wire framer decodes every envelope body
# (that is the transport convention for all of yetty's custom OSC/DCS codes).
#
# Post-color indices : 1 scanlines 2 crt 3 chromatic 4 broken-tv 5 matrix
#                      6 sepia 7 pixelate 8 wave 9 invert 10 night-vision
#                      11 vaporwave 12 thermal 13 glitch 14 emboss 15 rain
#                      16 matrix-rain 18 thunderstorm
# Coord indices      : 1 fisheye 2 magnify-cursor 3 magnify-mouse 4 warts
#                      5 wandering-wart 6 barrel 7 swirl 8 bulge 9 pinch
#                      10 jello 11 heartbeat 12 drunk 13 heat-haze 14 underwater
#                      15 earthquake 16 scanline-offset 17 vhs-tear 18 melt
#                      19 wave 20 funhouse 21 twist
# Per-effect demos live in effects/ (one script each).
#
# Usage:
#   source effects.sh          # defines yfx_post / yfx_coord / yfx_off
#   yfx_post 5 0.8 2           # matrix, green=0.8 speed=2
#   yfx_coord 6 0.4            # barrel, strength=0.4
#   yfx_off                    # clear all effects
#   ./effects.sh               # run the cycling demo

# Emit an effect OSC. $1 = OSC code, rest = INDEX and params.
_yfx_emit() {
    local code="$1"; shift
    local body
    body="$(IFS=:; echo "$*")"
    printf '\033]%s;%s\007' "$code" "$(printf '%s' "$body" | base64 | tr -d '\n')"
}

yfx_post()  { _yfx_emit 666668 "$@"; }   # post-color effect: INDEX P0..P5
yfx_coord() { _yfx_emit 666669 "$@"; }   # coord distortion:  INDEX P0 P1
yfx_off()   { _yfx_emit 666668 0; _yfx_emit 666669 0; }

# Fill the screen with sample text so an effect is visible. $1 = line count.
yfx_fill() {
    clear
    local n="${1:-20}" i
    for ((i = 1; i <= n; i++)); do
        echo "yetty effects :: line $i :: the quick brown fox jumps over the lazy dog 0123456789"
    done
}

# When executed (not sourced), run a short guided demo of all effects.
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    step() { echo; echo ">>> $1"; sleep "${2:-3}"; }

    yfx_fill; step "matrix (post 5)"       ; yfx_post 5 0.8 2 ; sleep 3
    yfx_fill; step "crt (post 2)"          ; yfx_post 2       ; sleep 3
    yfx_fill; step "night-vision (post 10)"; yfx_post 10      ; sleep 3
    yfx_fill; step "thermal (post 12)"     ; yfx_post 12      ; sleep 3
    yfx_post 0
    yfx_fill; step "barrel (coord 6)"      ; yfx_coord 6 0.4  ; sleep 3
    yfx_fill; step "fisheye (coord 1)"     ; yfx_coord 1 0.6  ; sleep 3
    yfx_fill; step "swirl (coord 7)"       ; yfx_coord 7 3.0  ; sleep 4
    yfx_off
    yfx_fill; echo; echo ">>> effects cleared"
fi
