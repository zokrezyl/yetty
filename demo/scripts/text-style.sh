#!/bin/bash
# Terminal: ANSI escape code text styles (bold, italic, underline, strikethrough)

echo ""
echo "=== YETTY TEXT STYLE DEMO ==="
echo ""

echo "--- Basic Styles ---"
echo "Normal text"
echo -e "\e[1mBold text\e[0m"
echo -e "\e[3mItalic text\e[0m"
echo -e "\e[1m\e[3mBold + Italic text\e[0m"
echo ""

echo "--- Underline Styles ---"
echo -e "\e[4mSingle underline\e[0m"
echo -e "\e[4:2mDouble underline\e[0m"
echo -e "\e[4:3mCurly/wavy underline\e[0m"
echo ""

echo "--- Strikethrough ---"
echo -e "\e[9mStrikethrough text\e[0m"
echo -e "\e[1m\e[9mBold + Strikethrough\e[0m"
echo ""

echo "--- Combined Styles ---"
echo -e "\e[1m\e[4mBold + Underline\e[0m"
echo -e "\e[3m\e[4mItalic + Underline\e[0m"
echo -e "\e[1m\e[3m\e[4mBold + Italic + Underline\e[0m"
echo -e "\e[1m\e[3m\e[4m\e[9mAll styles combined!\e[0m"
echo ""

echo "--- With Colors ---"
echo -e "\e[1m\e[31mBold Red\e[0m"
echo -e "\e[3m\e[32mItalic Green\e[0m"
echo -e "\e[4m\e[34mUnderlined Blue\e[0m"
echo -e "\e[9m\e[35mStrikethrough Magenta\e[0m"
echo -e "\e[1m\e[3m\e[4m\e[33mBold Italic Underlined Yellow\e[0m"
echo ""

echo "--- Reverse Video ---"
echo -e "\e[7mReverse video\e[0m"
echo -e "\e[1m\e[7mBold + Reverse\e[0m"
echo -e "\e[4m\e[7mUnderline + Reverse\e[0m"
echo ""

echo "--- Sample Text ---"
echo -e "The \e[1mquick\e[0m \e[33mbrown\e[0m \e[3mfox\e[0m \e[4mjumps\e[0m over the \e[9mlazy\e[0m dog."
echo ""

echo "--- Code Sample ---"
echo -e "fn \e[1mmain\e[0m() {"
echo -e "    let \e[3mx\e[0m = \e[32m42\e[0m;"
echo -e "    \e[4:3m// TODO: fix this\e[0m"
echo -e "    \e[9mprintln!(\"deprecated\");\e[0m"
echo -e "}"
echo ""

echo "=== END OF DEMO ==="
