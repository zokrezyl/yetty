#!/bin/bash
# Demo for the looping-spline shadertoy glyph.
#   wgsl:     src/yetty/yfont/glyph-shaders/0x00f6-looping-spline.wgsl
#   local_id: 0xf6  ->  PUA-B codepoint U+1000F6
# These shaders are tile-coherent (the fragment shader uses pixel_pos), so a
# block of cells renders one continuous scene instead of the cell repeated.
# Rendered as a tiled ROWS x COLS block. Needs the glyph listed under
# shaders/preload/glyphs (ships enabled).
#
#   ./build-desktop-ytrace-release/yetty \
#       -e demo/scripts/shader-glyph/shadertoy/0x00f6-looping-spline.sh
#
# Env: ROWS (16), COLS (64).
ROWS=${ROWS:-16}
COLS=${COLS:-64}
codepoint='\xf4\x80\x83\xb6'   # U+1000F6

printf '=== shadertoy glyph: looping-spline (local_id 0xf6) ===\n\n'
for ((row = 0; row < ROWS; row++)); do
    for ((col = 0; col < COLS; col++)); do printf "$codepoint"; done
    printf '\n'
done
