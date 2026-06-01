#!/bin/bash
# Terminal: ANSI escape code text styles (bold, italic, underline, strikethrough)

reset="\e[0m"
white="\e[97m"

echo ""
echo -e "${white}=== YETTY TEXT STYLE DEMO ===${reset}"
echo ""

echo -e "${white}--- Basic Styles ---${reset}"
echo -e "${white}NORMAL text expected:              -> ${reset}Normal text"
echo -e "${white}BOLD text expected:                -> ${reset}\e[1mBold text\e[0m"
echo -e "${white}ITALIC text expected:              -> ${reset}\e[3mItalic text\e[0m"
echo -e "${white}BOLD + ITALIC text expected:       -> ${reset}\e[1m\e[3mBold + Italic text\e[0m"
echo ""

echo -e "${white}--- Underline Styles ---${reset}"
echo -e "${white}SINGLE UNDERLINE expected:         -> ${reset}\e[4mSingle underline\e[0m"
echo -e "${white}DOUBLE UNDERLINE expected:         -> ${reset}\e[4:2mDouble underline\e[0m"
echo -e "${white}CURLY UNDERLINE expected:          -> ${reset}\e[4:3mCurly/wavy underline\e[0m"
echo ""

echo -e "${white}--- Strikethrough ---${reset}"
echo -e "${white}STRIKETHROUGH expected:            -> ${reset}\e[9mStrikethrough text\e[0m"
echo -e "${white}BOLD + STRIKETHROUGH expected:     -> ${reset}\e[1m\e[9mBold + Strikethrough\e[0m"
echo ""

echo -e "${white}--- Combined Styles ---${reset}"
echo -e "${white}BOLD + UNDERLINE expected:         -> ${reset}\e[1m\e[4mBold + Underline\e[0m"
echo -e "${white}ITALIC + UNDERLINE expected:       -> ${reset}\e[3m\e[4mItalic + Underline\e[0m"
echo -e "${white}BOLD+ITALIC+UNDERLINE expected:    -> ${reset}\e[1m\e[3m\e[4mBold + Italic + Underline\e[0m"
echo -e "${white}ALL STYLES expected:               -> ${reset}\e[1m\e[3m\e[4m\e[9mAll styles combined!\e[0m"
echo ""

echo -e "${white}--- Bold With Colors (ls --color uses this) ---${reset}"
echo -e "${white}BOLD RED expected:                 -> ${reset}\e[1m\e[31mBold Red\e[0m"
echo -e "${white}BOLD GREEN expected:               -> ${reset}\e[1m\e[32mBold Green\e[0m"
echo -e "${white}BOLD YELLOW expected:              -> ${reset}\e[1m\e[33mBold Yellow\e[0m"
echo -e "${white}BOLD BLUE expected:                -> ${reset}\e[1m\e[34mBold Blue\e[0m"
echo -e "${white}BOLD MAGENTA expected:             -> ${reset}\e[1m\e[35mBold Magenta\e[0m"
echo -e "${white}BOLD CYAN expected:                -> ${reset}\e[1m\e[36mBold Cyan\e[0m"
echo -e "${white}BOLD WHITE expected:               -> ${reset}\e[1m\e[37mBold White\e[0m"
echo ""

echo -e "${white}--- Italic With Colors ---${reset}"
echo -e "${white}ITALIC GREEN expected:             -> ${reset}\e[3m\e[32mItalic Green\e[0m"
echo -e "${white}ITALIC BLUE expected:              -> ${reset}\e[3m\e[34mItalic Blue\e[0m"
echo ""

echo -e "${white}--- Underline With Colors ---${reset}"
echo -e "${white}UNDERLINE BLUE expected:           -> ${reset}\e[4m\e[34mUnderlined Blue\e[0m"
echo -e "${white}UNDERLINE RED expected:            -> ${reset}\e[4m\e[31mUnderlined Red\e[0m"
echo ""

echo -e "${white}--- Strikethrough With Colors ---${reset}"
echo -e "${white}STRIKE MAGENTA expected:           -> ${reset}\e[9m\e[35mStrikethrough Magenta\e[0m"
echo ""

echo -e "${white}--- Complex Combinations ---${reset}"
echo -e "${white}BOLD+ITALIC+UNDERLINE+YELLOW:      -> ${reset}\e[1m\e[3m\e[4m\e[33mBold Italic Underlined Yellow\e[0m"
echo ""

echo -e "${white}--- Reverse Video ---${reset}"
echo -e "${white}REVERSE expected:                  -> ${reset}\e[7mReverse video\e[0m"
echo -e "${white}BOLD + REVERSE expected:           -> ${reset}\e[1m\e[7mBold + Reverse\e[0m"
echo -e "${white}UNDERLINE + REVERSE expected:      -> ${reset}\e[4m\e[7mUnderline + Reverse\e[0m"
echo ""

echo -e "${white}--- Sample Text ---${reset}"
echo -e "${white}Mixed styles in sentence:          -> ${reset}The \e[1mquick\e[0m \e[33mbrown\e[0m \e[3mfox\e[0m \e[4mjumps\e[0m over the \e[9mlazy\e[0m dog."
echo ""

echo -e "${white}--- Code Sample ---${reset}"
echo -e "${white}fn BOLD main():                    -> ${reset}fn \e[1mmain\e[0m() {"
echo -e "${white}    let ITALIC x = GREEN 42:       -> ${reset}    let \e[3mx\e[0m = \e[32m42\e[0m;"
echo -e "${white}    CURLY UNDERLINE comment:       -> ${reset}    \e[4:3m// TODO: fix this\e[0m"
echo -e "${white}    STRIKE deprecated:             -> ${reset}    \e[9mprintln!(\"deprecated\");\e[0m"
echo -e "${white}closing brace:                     -> ${reset}}"
echo ""

echo -e "${white}=== END OF DEMO ===${reset}"
