#!/bin/bash
# Tile a single shader glyph in a rows × cols block.
# Usage: tile.sh <name> [rows=10] [cols=79]
#        tile.sh plasma                # full plasma field
#        tile.sh biomine 20 80         # large fractal block
#
# All 46 glyph names are valid; mapping mirrors local_id assignment in
# src/yetty/yfont/glyph-shaders/.

set -e

declare -A cp_for=(
    [spinner]='\xf4\x80\x80\x80'              # 0x00
    [pulse]='\xf4\x80\x80\x81'                # 0x01
    [wave]='\xf4\x80\x80\x82'                 # 0x02
    [fire]='\xf4\x80\x80\x83'                 # 0x03
    [gem]='\xf4\x80\x80\x84'                  # 0x04
    [heart]='\xf4\x80\x80\x85'                # 0x05
    [party]='\xf4\x80\x80\x86'                # 0x06
    [plasma]='\xf4\x80\x80\x87'               # 0x07
    [ripple]='\xf4\x80\x80\x88'               # 0x08
    [rocket]='\xf4\x80\x80\x89'               # 0x09
    [smiley]='\xf4\x80\x80\x8a'               # 0x0a
    [sparkle]='\xf4\x80\x80\x8b'              # 0x0b
    [star]='\xf4\x80\x80\x8c'                 # 0x0c
    [watch]='\xf4\x80\x80\x92'                # 0x12
    [spiral]='\xf4\x80\x80\x93'               # 0x13
    [border-dots]='\xf4\x80\x80\x95'          # 0x15
    [orbit-dots]='\xf4\x80\x80\x96'           # 0x16
    [border-dots-grouped]='\xf4\x80\x80\x97'  # 0x17
    [worm]='\xf4\x80\x80\x98'                 # 0x18
    [bouncing-ball]='\xf4\x80\x80\x99'        # 0x19
    [radar-sweep]='\xf4\x80\x80\x9a'          # 0x1a
    [hourglass]='\xf4\x80\x80\x9b'            # 0x1b
    [dna-helix]='\xf4\x80\x80\x9c'            # 0x1c
    [equalizer]='\xf4\x80\x80\x9d'            # 0x1d
    [pendulum]='\xf4\x80\x80\x9e'             # 0x1e
    [conveyor-dots]='\xf4\x80\x80\x9f'        # 0x1f
    [pong]='\xf4\x80\x80\xa0'                 # 0x20
    [rain]='\xf4\x80\x80\xa1'                 # 0x21
    [orbit]='\xf4\x80\x80\xa2'                # 0x22
    [crosshair-pulse]='\xf4\x80\x80\xa3'      # 0x23
    [matrix-rain]='\xf4\x80\x80\xa4'          # 0x24
    [metaballs]='\xf4\x80\x80\xa5'            # 0x25
    [heartbeat]='\xf4\x80\x80\xa6'            # 0x26
    [typing-dots]='\xf4\x80\x80\xa7'          # 0x27
    [signal-bars]='\xf4\x80\x80\xa8'          # 0x28
    [vinyl]='\xf4\x80\x80\xa9'                # 0x29
    [dvd-bounce]='\xf4\x80\x80\xaa'           # 0x2a
    [butterfly-flock]='\xf4\x80\x83\xb5'      # 0xf5
    [looping-spline]='\xf4\x80\x83\xb6'       # 0xf6
    [mandelbrot-deco]='\xf4\x80\x83\xb7'      # 0xf7
    [hg-sdf]='\xf4\x80\x83\xb8'               # 0xf8
    [voronoi]='\xf4\x80\x83\xb9'              # 0xf9
    [minkowski-tube]='\xf4\x80\x83\xba'       # 0xfa
    [biomine]='\xf4\x80\x83\xbb'              # 0xfb
    [julia]='\xf4\x80\x83\xbc'                # 0xfc
    [mandelbrot]='\xf4\x80\x83\xbd'           # 0xfd
)

name="${1:-plasma}"
rows="${2:-10}"
cols="${3:-79}"

cp="${cp_for[$name]:-}"
if [[ -z "$cp" ]]; then
    printf "tile.sh: unknown glyph '%s'\n\nValid names:\n" "$name" >&2
    for n in "${!cp_for[@]}"; do echo "  $n"; done | sort >&2
    exit 1
fi

for ((row = 0; row < rows; row++)); do
    for ((col = 0; col < cols; col++)); do
        printf "$cp"
    done
    printf '\n'
done
