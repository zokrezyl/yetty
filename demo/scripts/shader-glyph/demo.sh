#!/bin/bash
# Shader-glyph showcase. No `clear`, no `cat`, no `bc`. Just `printf` + sleep.
# Each section just APPENDS to the screen so cells accumulate visibly.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/shader-glyph/demo.sh

DEMO_PAUSE=${DEMO_PAUSE:-3}

p() { sleep "$DEMO_PAUSE"; }

# helper: print cell N times then newline
row() {
    local cp="$1"; local n="${2:-30}"
    for ((i = 0; i < n; i++)); do printf "$cp"; done
    printf '\n'
}
# helper: tile rows×cols
tile() {
    local cp="$1"; local rows="${2:-4}"; local cols="${3:-30}"
    for ((r = 0; r < rows; r++)); do row "$cp" "$cols"; done
}

printf '=== SHADER-GLYPH SHOWCASE ===\n\n'

# ---- 1. Simple animated glyphs ----
printf 'simple animated glyphs (left to right: spinner pulse fire heart star)\n'
for ((r = 0; r < 3; r++)); do
    for ((i = 0; i < 6; i++)); do printf '\xf4\x80\x80\x80'; done    # spinner
    for ((i = 0; i < 6; i++)); do printf '\xf4\x80\x80\x81'; done    # pulse
    for ((i = 0; i < 6; i++)); do printf '\xf4\x80\x80\x83'; done    # fire
    for ((i = 0; i < 6; i++)); do printf '\xf4\x80\x80\x85'; done    # heart
    for ((i = 0; i < 6; i++)); do printf '\xf4\x80\x80\x8c'; done    # star
    printf '\n'
done
printf '\n'
p

# ---- 2. Tile-coherent effects ----
printf 'plasma (tile-coherent rainbow flow)\n'
tile '\xf4\x80\x80\x87' 4 60
printf '\n'
p

printf 'wave (audio bars flowing across cells)\n'
tile '\xf4\x80\x80\x82' 4 60
printf '\n'
p

# ---- 3. Fractal ----
printf 'mandelbrot fractal (across all cells in this block)\n'
tile '\xf4\x80\x83\xbd' 6 60
printf '\n'
p

printf 'biomine (raymarched scene)\n'
tile '\xf4\x80\x83\xbb' 6 60
printf '\n'
p

# ---- 4. Inline ----
printf 'inline status indicators:\n'
printf '  Loading  '; printf '\xf4\x80\x80\x80'; printf '   data\n'
printf '  Status   '; printf '\xf4\x80\x80\x81'; printf '   ok\n'
printf '  Health   '; printf '\xf4\x80\x80\x85'; printf '   beating\n'
printf '  Build    '; printf '\xf4\x80\x80\x83'; printf '   in progress\n'
printf '  Done     '; printf '\xf4\x80\x80\x8c'; printf '   rated\n'
printf '\n'

printf '=== showcase complete - holding open ===\n'
sleep 600
