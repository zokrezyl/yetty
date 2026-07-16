#!/bin/bash
# HarfBuzz demo — Arabic: cursive joining and right-to-left order.
#
# Every Arabic letter has up to four contextual shapes (isolated, initial,
# medial, final) chosen by OpenType GSUB joining. Without shaping each codepoint
# draws its isolated form, so a word reads as disconnected stumps; with shaping
# it becomes one joined cursive run. Harakat (vowel marks) sit on their base.
# See ./README.md for what renders on the grid today vs. through the shaper.

reset="\e[0m"
title="\e[1m\e[97m"
rom="\e[2m\e[37m"
zwj=$'‍' # zero-width joiner: forces a letter into a joined shape

echo ""
echo -e "${title}=== HarfBuzz · Arabic — cursive joining (RTL) ===${reset}"
echo ""

echo -e "${rom}\"peace be upon you\" — as-salamu alaykum${reset}"
echo    "    السلام عليكم"
echo ""

echo -e "${rom}\"the Arabic language\" — al-lugha al-arabiyya${reset}"
echo    "    اللغة العربية"
echo ""

echo -e "${rom}\"hello world\" — marhaban ayyuha al-alam${reset}"
echo    "    مرحبا أيها العالم"
echo ""

echo -e "${rom}with harakat (vowel marks) — muhammad${reset}"
echo    "    مُحَمَّد"
echo ""

echo -e "${title}--- the four joining shapes of one letter (beh, ب) ---${reset}"
echo -e "${rom}isolated · initial · medial · final${reset}"
echo    "    ب    ب${zwj}    ${zwj}ب${zwj}    ${zwj}ب"
echo ""

echo -e "${title}=== end ===${reset}"
