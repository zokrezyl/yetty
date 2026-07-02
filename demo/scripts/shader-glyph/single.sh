#!/bin/bash
# Single-glyph diagnostic.
#
# Prints ONE codepoint and sleeps. Visual pass criteria:
#   plasma  -> rainbow gradient at top-left, animated, slowly cycling
#   spinner -> cyan ring with three rotating blobs
#   heart   -> red pulsing heart (so "pink" is correct here)
#   star    -> golden glowing star
#
# Usage:  ./single.sh [name]   # default: plasma
#
# Run inside yetty:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/shader-glyph/single.sh
#   ./build-desktop-ytrace-release/yetty -e 'demo/scripts/shader-glyph/single.sh spinner'

# UTF-8 for U+100000 + local_id (PUA-B). Earlier the table sat at U+E000
# in BMP PUA, which collided with Powerline/Nerd-Fonts icons — moved to
# PUA-B (verified empty in the bundled DejaVuSansM Nerd Font) so any
# Nerd-Font glyph the prompt draws no longer pins the shader-glyph
# animation timer on.
#   U+100000 → \xf4\x80\x80\x80
#   U+1000NN → \xf4\x80\x{82 + NN/64}\x{0x80 | (NN & 63)}
declare -A cp_for=(
    [spinner]='\xf4\x80\x80\x80'
    [pulse]='\xf4\x80\x80\x81'
    [plasma]='\xf4\x80\x80\x87'
    [heart]='\xf4\x80\x80\x85'
    [fire]='\xf4\x80\x80\x83'
    [star]='\xf4\x80\x80\x8c'
    [mandelbrot]='\xf4\x80\x83\xbd'
    [biomine]='\xf4\x80\x83\xbb'
)

name="${1:-plasma}"
cp="${cp_for[$name]:-${cp_for[plasma]}}"

clear
printf 'glyph: %s  (cp=%s)\n\n' "$name" "$cp"
# A small block so it's hard to miss
for r in 1 2 3 4; do
    for c in $(seq 1 20); do
        printf "$cp"
    done
    printf '\n'
done
echo
echo 'sleeping 30s — Ctrl+C to exit'
sleep "${DEMO_HOLD:-30}"
