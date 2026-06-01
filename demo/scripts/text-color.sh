#!/bin/bash
# Text color demo script for yetty terminal testing

reset="\e[0m"
white="\e[97m"

# Basic foreground colors
fg_basic() {
    echo -e "${white}=== Basic Foreground Colors ===${reset}"
    echo -e "${white}FG: black(30) BG: default         -> ${reset}\e[30mSample Text${reset}"
    echo -e "${white}FG: black(30) BG: default REVERSE -> ${reset}\e[7;30mSample Text${reset}"
    echo -e "${white}FG: red(31) BG: default           -> ${reset}\e[31mSample Text${reset}"
    echo -e "${white}FG: red(31) BG: default REVERSE   -> ${reset}\e[7;31mSample Text${reset}"
    echo -e "${white}FG: green(32) BG: default         -> ${reset}\e[32mSample Text${reset}"
    echo -e "${white}FG: green(32) BG: default REVERSE -> ${reset}\e[7;32mSample Text${reset}"
    echo -e "${white}FG: yellow(33) BG: default        -> ${reset}\e[33mSample Text${reset}"
    echo -e "${white}FG: yellow(33) BG: default REVERSE-> ${reset}\e[7;33mSample Text${reset}"
    echo -e "${white}FG: blue(34) BG: default          -> ${reset}\e[34mSample Text${reset}"
    echo -e "${white}FG: blue(34) BG: default REVERSE  -> ${reset}\e[7;34mSample Text${reset}"
    echo -e "${white}FG: magenta(35) BG: default       -> ${reset}\e[35mSample Text${reset}"
    echo -e "${white}FG: magenta(35) BG: default REVERSE-> ${reset}\e[7;35mSample Text${reset}"
    echo -e "${white}FG: cyan(36) BG: default          -> ${reset}\e[36mSample Text${reset}"
    echo -e "${white}FG: cyan(36) BG: default REVERSE  -> ${reset}\e[7;36mSample Text${reset}"
    echo -e "${white}FG: white(37) BG: default         -> ${reset}\e[37mSample Text${reset}"
    echo -e "${white}FG: white(37) BG: default REVERSE -> ${reset}\e[7;37mSample Text${reset}"
    echo
}

# Bright foreground colors
fg_bright() {
    echo -e "${white}=== Bright Foreground Colors ===${reset}"
    echo -e "${white}FG: bright black(90) BG: default         -> ${reset}\e[90mSample Text${reset}"
    echo -e "${white}FG: bright black(90) BG: default REVERSE -> ${reset}\e[7;90mSample Text${reset}"
    echo -e "${white}FG: bright red(91) BG: default           -> ${reset}\e[91mSample Text${reset}"
    echo -e "${white}FG: bright red(91) BG: default REVERSE   -> ${reset}\e[7;91mSample Text${reset}"
    echo -e "${white}FG: bright green(92) BG: default         -> ${reset}\e[92mSample Text${reset}"
    echo -e "${white}FG: bright green(92) BG: default REVERSE -> ${reset}\e[7;92mSample Text${reset}"
    echo -e "${white}FG: bright yellow(93) BG: default        -> ${reset}\e[93mSample Text${reset}"
    echo -e "${white}FG: bright yellow(93) BG: default REVERSE-> ${reset}\e[7;93mSample Text${reset}"
    echo -e "${white}FG: bright blue(94) BG: default          -> ${reset}\e[94mSample Text${reset}"
    echo -e "${white}FG: bright blue(94) BG: default REVERSE  -> ${reset}\e[7;94mSample Text${reset}"
    echo -e "${white}FG: bright magenta(95) BG: default       -> ${reset}\e[95mSample Text${reset}"
    echo -e "${white}FG: bright magenta(95) BG: default REVERSE-> ${reset}\e[7;95mSample Text${reset}"
    echo -e "${white}FG: bright cyan(96) BG: default          -> ${reset}\e[96mSample Text${reset}"
    echo -e "${white}FG: bright cyan(96) BG: default REVERSE  -> ${reset}\e[7;96mSample Text${reset}"
    echo -e "${white}FG: bright white(97) BG: default         -> ${reset}\e[97mSample Text${reset}"
    echo -e "${white}FG: bright white(97) BG: default REVERSE -> ${reset}\e[7;97mSample Text${reset}"
    echo
}

# Basic background colors
bg_basic() {
    echo -e "${white}=== Basic Background Colors ===${reset}"
    echo -e "${white}FG: default BG: black(40)         -> ${reset}\e[40m Sample Text ${reset}"
    echo -e "${white}FG: default BG: black(40) REVERSE -> ${reset}\e[7;40m Sample Text ${reset}"
    echo -e "${white}FG: default BG: red(41)           -> ${reset}\e[41m Sample Text ${reset}"
    echo -e "${white}FG: default BG: red(41) REVERSE   -> ${reset}\e[7;41m Sample Text ${reset}"
    echo -e "${white}FG: default BG: green(42)         -> ${reset}\e[42m Sample Text ${reset}"
    echo -e "${white}FG: default BG: green(42) REVERSE -> ${reset}\e[7;42m Sample Text ${reset}"
    echo -e "${white}FG: default BG: yellow(43)        -> ${reset}\e[43m Sample Text ${reset}"
    echo -e "${white}FG: default BG: yellow(43) REVERSE-> ${reset}\e[7;43m Sample Text ${reset}"
    echo -e "${white}FG: default BG: blue(44)          -> ${reset}\e[44m Sample Text ${reset}"
    echo -e "${white}FG: default BG: blue(44) REVERSE  -> ${reset}\e[7;44m Sample Text ${reset}"
    echo -e "${white}FG: default BG: magenta(45)       -> ${reset}\e[45m Sample Text ${reset}"
    echo -e "${white}FG: default BG: magenta(45) REVERSE-> ${reset}\e[7;45m Sample Text ${reset}"
    echo -e "${white}FG: default BG: cyan(46)          -> ${reset}\e[46m Sample Text ${reset}"
    echo -e "${white}FG: default BG: cyan(46) REVERSE  -> ${reset}\e[7;46m Sample Text ${reset}"
    echo -e "${white}FG: default BG: white(47)         -> ${reset}\e[47m Sample Text ${reset}"
    echo -e "${white}FG: default BG: white(47) REVERSE -> ${reset}\e[7;47m Sample Text ${reset}"
    echo
}

# Bright background colors
bg_bright() {
    echo -e "${white}=== Bright Background Colors ===${reset}"
    echo -e "${white}FG: default BG: bright black(100)   -> ${reset}\e[100m Sample Text ${reset}"
    echo -e "${white}FG: default BG: bright red(101)     -> ${reset}\e[101m Sample Text ${reset}"
    echo -e "${white}FG: default BG: bright green(102)   -> ${reset}\e[102m Sample Text ${reset}"
    echo -e "${white}FG: default BG: bright yellow(103)  -> ${reset}\e[103m Sample Text ${reset}"
    echo -e "${white}FG: default BG: bright blue(104)    -> ${reset}\e[104m Sample Text ${reset}"
    echo -e "${white}FG: default BG: bright magenta(105) -> ${reset}\e[105m Sample Text ${reset}"
    echo -e "${white}FG: default BG: bright cyan(106)    -> ${reset}\e[106m Sample Text ${reset}"
    echo -e "${white}FG: default BG: bright white(107)   -> ${reset}\e[107m Sample Text ${reset}"
    echo
}

# Combined FG + BG
fg_bg_combined() {
    echo -e "${white}=== Combined FG + BG ===${reset}"
    echo -e "${white}FG: red(31) BG: green(42)         -> ${reset}\e[31;42m Sample Text ${reset}"
    echo -e "${white}FG: red(31) BG: green(42) REVERSE -> ${reset}\e[7;31;42m Sample Text ${reset}"
    echo -e "${white}FG: yellow(33) BG: blue(44)       -> ${reset}\e[33;44m Sample Text ${reset}"
    echo -e "${white}FG: yellow(33) BG: blue(44) REV   -> ${reset}\e[7;33;44m Sample Text ${reset}"
    echo -e "${white}FG: white(37) BG: red(41)         -> ${reset}\e[37;41m Sample Text ${reset}"
    echo -e "${white}FG: white(37) BG: red(41) REVERSE -> ${reset}\e[7;37;41m Sample Text ${reset}"
    echo -e "${white}FG: black(30) BG: white(47)       -> ${reset}\e[30;47m Sample Text ${reset}"
    echo -e "${white}FG: black(30) BG: white(47) REV   -> ${reset}\e[7;30;47m Sample Text ${reset}"
    echo -e "${white}FG: green(32) BG: black(40)       -> ${reset}\e[32;40m Sample Text ${reset}"
    echo -e "${white}FG: green(32) BG: black(40) REV   -> ${reset}\e[7;32;40m Sample Text ${reset}"
    echo -e "${white}FG: cyan(36) BG: magenta(45)      -> ${reset}\e[36;45m Sample Text ${reset}"
    echo -e "${white}FG: cyan(36) BG: magenta(45) REV  -> ${reset}\e[7;36;45m Sample Text ${reset}"
    echo
}

# 256 color foreground
fg_256() {
    echo -e "${white}=== 256 Color Foreground ===${reset}"
    echo -e "${white}FG: 196(red) BG: default         -> ${reset}\e[38;5;196mSample Text${reset}"
    echo -e "${white}FG: 196(red) BG: default REVERSE -> ${reset}\e[7;38;5;196mSample Text${reset}"
    echo -e "${white}FG: 46(green) BG: default        -> ${reset}\e[38;5;46mSample Text${reset}"
    echo -e "${white}FG: 46(green) BG: default REV    -> ${reset}\e[7;38;5;46mSample Text${reset}"
    echo -e "${white}FG: 21(blue) BG: default         -> ${reset}\e[38;5;21mSample Text${reset}"
    echo -e "${white}FG: 21(blue) BG: default REVERSE -> ${reset}\e[7;38;5;21mSample Text${reset}"
    echo -e "${white}FG: 226(yellow) BG: default      -> ${reset}\e[38;5;226mSample Text${reset}"
    echo -e "${white}FG: 226(yellow) BG: default REV  -> ${reset}\e[7;38;5;226mSample Text${reset}"
    echo -e "${white}FG: 201(magenta) BG: default     -> ${reset}\e[38;5;201mSample Text${reset}"
    echo -e "${white}FG: 201(magenta) BG: default REV -> ${reset}\e[7;38;5;201mSample Text${reset}"
    echo -e "${white}FG: 51(cyan) BG: default         -> ${reset}\e[38;5;51mSample Text${reset}"
    echo -e "${white}FG: 51(cyan) BG: default REVERSE -> ${reset}\e[7;38;5;51mSample Text${reset}"
    echo
}

# 256 color background
bg_256() {
    echo -e "${white}=== 256 Color Background ===${reset}"
    echo -e "${white}FG: default BG: 196(red)         -> ${reset}\e[48;5;196m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 196(red) REVERSE -> ${reset}\e[7;48;5;196m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 46(green)        -> ${reset}\e[48;5;46m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 46(green) REV    -> ${reset}\e[7;48;5;46m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 21(blue)         -> ${reset}\e[48;5;21m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 21(blue) REVERSE -> ${reset}\e[7;48;5;21m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 226(yellow)      -> ${reset}\e[48;5;226m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 226(yellow) REV  -> ${reset}\e[7;48;5;226m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 201(magenta)     -> ${reset}\e[48;5;201m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 201(magenta) REV -> ${reset}\e[7;48;5;201m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 51(cyan)         -> ${reset}\e[48;5;51m Sample Text ${reset}"
    echo -e "${white}FG: default BG: 51(cyan) REVERSE -> ${reset}\e[7;48;5;51m Sample Text ${reset}"
    echo
}

# True color foreground
fg_rgb() {
    echo -e "${white}=== True Color (RGB) Foreground ===${reset}"
    echo -e "${white}FG: RGB(255,0,0) BG: default       -> ${reset}\e[38;2;255;0;0mSample Text${reset}"
    echo -e "${white}FG: RGB(255,0,0) BG: default REV   -> ${reset}\e[7;38;2;255;0;0mSample Text${reset}"
    echo -e "${white}FG: RGB(0,255,0) BG: default       -> ${reset}\e[38;2;0;255;0mSample Text${reset}"
    echo -e "${white}FG: RGB(0,255,0) BG: default REV   -> ${reset}\e[7;38;2;0;255;0mSample Text${reset}"
    echo -e "${white}FG: RGB(0,0,255) BG: default       -> ${reset}\e[38;2;0;0;255mSample Text${reset}"
    echo -e "${white}FG: RGB(0,0,255) BG: default REV   -> ${reset}\e[7;38;2;0;0;255mSample Text${reset}"
    echo -e "${white}FG: RGB(255,255,0) BG: default     -> ${reset}\e[38;2;255;255;0mSample Text${reset}"
    echo -e "${white}FG: RGB(255,255,0) BG: default REV -> ${reset}\e[7;38;2;255;255;0mSample Text${reset}"
    echo -e "${white}FG: RGB(255,0,255) BG: default     -> ${reset}\e[38;2;255;0;255mSample Text${reset}"
    echo -e "${white}FG: RGB(255,0,255) BG: default REV -> ${reset}\e[7;38;2;255;0;255mSample Text${reset}"
    echo -e "${white}FG: RGB(0,255,255) BG: default     -> ${reset}\e[38;2;0;255;255mSample Text${reset}"
    echo -e "${white}FG: RGB(0,255,255) BG: default REV -> ${reset}\e[7;38;2;0;255;255mSample Text${reset}"
    echo
}

# True color background
bg_rgb() {
    echo -e "${white}=== True Color (RGB) Background ===${reset}"
    echo -e "${white}FG: default BG: RGB(255,0,0)       -> ${reset}\e[48;2;255;0;0m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(255,0,0) REV   -> ${reset}\e[7;48;2;255;0;0m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(0,255,0)       -> ${reset}\e[48;2;0;255;0m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(0,255,0) REV   -> ${reset}\e[7;48;2;0;255;0m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(0,0,255)       -> ${reset}\e[48;2;0;0;255m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(0,0,255) REV   -> ${reset}\e[7;48;2;0;0;255m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(255,255,0)     -> ${reset}\e[48;2;255;255;0m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(255,255,0) REV -> ${reset}\e[7;48;2;255;255;0m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(255,0,255)     -> ${reset}\e[48;2;255;0;255m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(255,0,255) REV -> ${reset}\e[7;48;2;255;0;255m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(0,255,255)     -> ${reset}\e[48;2;0;255;255m Sample Text ${reset}"
    echo -e "${white}FG: default BG: RGB(0,255,255) REV -> ${reset}\e[7;48;2;0;255;255m Sample Text ${reset}"
    echo
}

# Pure reverse video tests
reverse_only() {
    echo -e "${white}=== Pure Reverse Video Tests ===${reset}"
    echo -e "${white}Normal text (no attributes)       -> ${reset}Sample Text"
    echo -e "${white}REVERSE only (7)                  -> ${reset}\e[7mSample Text${reset}"
    echo -e "${white}FG: default BG: default REVERSE   -> ${reset}\e[7mSample Text${reset}"
    echo -e "${white}Bold only (1)                     -> ${reset}\e[1mSample Text${reset}"
    echo -e "${white}Bold + REVERSE                    -> ${reset}\e[1;7mSample Text${reset}"
    echo -e "${white}Dim only (2)                      -> ${reset}\e[2mSample Text${reset}"
    echo -e "${white}Dim + REVERSE                     -> ${reset}\e[2;7mSample Text${reset}"
    echo -e "${white}Italic only (3)                   -> ${reset}\e[3mSample Text${reset}"
    echo -e "${white}Underline only (4)                -> ${reset}\e[4mSample Text${reset}"
    echo
}

# Default color with reverse
default_reverse() {
    echo -e "${white}=== Default Colors with Reverse ===${reset}"
    echo -e "${white}FG: default(39) BG: default       -> ${reset}\e[39mSample Text${reset}"
    echo -e "${white}FG: default(39) BG: default REV   -> ${reset}\e[7;39mSample Text${reset}"
    echo -e "${white}FG: default BG: default(49)       -> ${reset}\e[49mSample Text${reset}"
    echo -e "${white}FG: default BG: default(49) REV   -> ${reset}\e[7;49mSample Text${reset}"
    echo
}

# Bold with colors (often uses bright palette)
bold_colors() {
    echo -e "${white}=== Bold with Basic Colors ===${reset}"
    echo -e "${white}FG: bold+black(1;30) BG: default  -> ${reset}\e[1;30mSample Text${reset}"
    echo -e "${white}FG: bold+red(1;31) BG: default    -> ${reset}\e[1;31mSample Text${reset}"
    echo -e "${white}FG: bold+green(1;32) BG: default  -> ${reset}\e[1;32mSample Text${reset}"
    echo -e "${white}FG: bold+yellow(1;33) BG: default -> ${reset}\e[1;33mSample Text${reset}"
    echo -e "${white}FG: bold+blue(1;34) BG: default   -> ${reset}\e[1;34mSample Text${reset}"
    echo -e "${white}FG: bold+magenta(1;35) BG: default-> ${reset}\e[1;35mSample Text${reset}"
    echo -e "${white}FG: bold+cyan(1;36) BG: default   -> ${reset}\e[1;36mSample Text${reset}"
    echo -e "${white}FG: bold+white(1;37) BG: default  -> ${reset}\e[1;37mSample Text${reset}"
    echo
}

# Run demos
reverse_only
default_reverse
bold_colors
fg_basic
fg_bright
bg_basic
bg_bright
fg_bg_combined
fg_256
bg_256
fg_rgb
bg_rgb
