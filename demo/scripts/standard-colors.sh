#!/bin/bash
# Standard ANSI 16-colour demo for yetty terminal testing.
#
# Prints the 8 normal + 8 bright ANSI colours as solid background blocks
# alongside the truecolor triple each *should* resolve to in a default
# xterm-style palette. If the indexed block and the RGB block to its right
# look different, the terminal is resolving the 16-colour palette wrong.

reset=$'\e[0m'

# name  fg-code  bg-code  expected-RGB (yetty's soft default palette;
#                                       override via terminal/colors/* config)
rows=(
    "black        30 40   29  31  33"
    "red          31 41   204 102 102"
    "green        32 42   181 189 104"
    "yellow       33 43   240 198 116"
    "blue         34 44   129 162 190"
    "magenta      35 45   178 148 187"
    "cyan         36 46   138 190 183"
    "white        37 47   197 200 198"
    "br-black     90 100  102 102 102"
    "br-red       91 101  213 78  83"
    "br-green     92 102  185 202 74"
    "br-yellow    93 103  231 197 71"
    "br-blue      94 104  122 166 218"
    "br-magenta   95 105  195 151 216"
    "br-cyan      96 106  112 192 186"
    "br-white     97 107  234 234 234"
)

printf '%s=== Standard 16 ANSI colours ===\n' "$reset"
printf '%s%-12s  %-7s   %-8s   %s\n' "$reset" "name" "FG text" "BG block" "RGB block (expected)"

for row in "${rows[@]}"; do
    read -r name fg bg r g b <<<"$row"
    # foreground sample
    fg_sample=$(printf '\e[%dmAaBbCc123%s' "$fg" "$reset")
    # indexed background block (8 wide)
    bg_block=$(printf '\e[%dm        %s' "$bg" "$reset")
    # truecolor block of the expected RGB (8 wide)
    rgb_block=$(printf '\e[48;2;%d;%d;%dm        %s' "$r" "$g" "$b" "$reset")
    printf '%s%-12s  %b   %b   %b  (%d,%d,%d)\n' \
        "$reset" "$name" "$fg_sample" "$bg_block" "$rgb_block" "$r" "$g" "$b"
done
printf '\n'
