#!/bin/bash
# Color matrix demo for yetty terminal testing.
#
# Prints, top to bottom:
#   1. RGB truecolor ramps  (R, G, B, grey)        -- SGR 48;2;r;g;b
#   2. RGB truecolor hue sweep                      -- SGR 48;2;r;g;b
#   3. xterm-256 palette matrix (16 + 6x6x6 + grey) -- SGR 48;5;n
#
# The 256-color cube is laid out so that a given block has a known,
# well-defined RGB value. That lets us compare, side by side, how the
# terminal renders an *indexed* colour against the *truecolor* triple it
# is supposed to resolve to -- the two must match.

reset=$'\e[0m'

# Terminal width — one gradient step per column gives a smooth ramp (the
# Alacritty-style demos do exactly this). Fall back to 80 if tput is absent.
cols=$( (tput cols) 2>/dev/null || echo 80 )
[ "$cols" -gt 0 ] 2>/dev/null || cols=80

# Emit one background block (two spaces) in a truecolor.
rgb_block() { printf '\e[48;2;%d;%d;%dm  ' "$1" "$2" "$3"; }
# Emit one background block (two spaces) in an indexed 256-colour.
idx_block() { printf '\e[48;5;%dm  ' "$1"; }

heading() { printf '%s\n' "${reset}$1"; }

# --- 1. Truecolor ramps -----------------------------------------------------
# One step per column across the full width — a smooth 0..255 gradient with as
# many shades as the terminal is wide. A truecolor terminal renders every step
# distinctly (no banding); an 8-bit/256-colour terminal would show visible bands.
rgb_ramps() {
    heading "=== Truecolor ramps (48;2;r;g;b) — R / G / B / grey ==="
    local i v n=$cols
    for chan in R G B W; do
        for ((i = 0; i < n; i++)); do
            v=$((i * 255 / (n - 1)))
            case $chan in
                R) printf '\e[48;2;%d;0;0m ' "$v" ;;
                G) printf '\e[48;2;0;%d;0m ' "$v" ;;
                B) printf '\e[48;2;0;0;%dm ' "$v" ;;
                W) printf '\e[48;2;%d;%d;%dm ' "$v" "$v" "$v" ;;
            esac
        done
        printf '%s %s\n' "$reset" "$chan"
    done
    printf '\n'
}

# --- 2. Truecolor hue sweep -------------------------------------------------
# A full-saturation rainbow, one step per column (HSV with S=V=1).
rgb_hue() {
    heading "=== Truecolor hue sweep (48;2;r;g;b) ==="
    local i h r g b n=$cols
    for ((i = 0; i < n; i++)); do
        h=$((i * 360 / n))
        # HSV->RGB for S=V=1, integer math, 60-degree sectors.
        local seg=$((h / 60)) f=$((h % 60))
        local up=$((f * 255 / 60)) dn=$((255 - f * 255 / 60))
        case $seg in
            0) r=255; g=$up;  b=0 ;;
            1) r=$dn; g=255; b=0 ;;
            2) r=0;   g=255; b=$up ;;
            3) r=0;   g=$dn; b=255 ;;
            4) r=$up; g=0;   b=255 ;;
            *) r=255; g=0;   b=$dn ;;
        esac
        printf '\e[48;2;%d;%d;%dm ' "$r" "$g" "$b"
    done
    printf '%s\n\n' "$reset"
}

# --- 3. xterm-256 palette matrix --------------------------------------------
idx_matrix() {
    heading "=== xterm-256 indexed matrix (48;5;n) ==="

    # System colours 0..15
    local n
    for ((n = 0; n < 16; n++)); do idx_block "$n"; done
    printf '%s  0..15 (system)\n' "$reset"

    # 6x6x6 cube: 36 colours per row, 6 rows (indices 16..231).
    for ((g6 = 0; g6 < 6; g6++)); do
        for ((r6 = 0; r6 < 6; r6++)); do
            for ((b6 = 0; b6 < 6; b6++)); do
                n=$((16 + 36 * r6 + 6 * g6 + b6))
                idx_block "$n"
            done
        done
        printf '%s\n' "$reset"
    done
    printf '%s  16..231 (6x6x6 cube)\n' "$reset"

    # Greyscale ramp 232..255
    for ((n = 232; n < 256; n++)); do idx_block "$n"; done
    printf '%s  232..255 (grey)\n\n' "$reset"
}

rgb_ramps
rgb_hue
idx_matrix
