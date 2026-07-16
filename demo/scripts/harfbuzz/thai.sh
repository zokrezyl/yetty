#!/bin/bash
# HarfBuzz demo — Thai: stacked above/below vowels and tone marks.
#
# Thai writes without spaces between words and stacks up to two marks over or
# under a base consonant (an upper vowel plus a tone mark). The shaper positions
# each mark relative to the base and to any mark already placed; per-codepoint
# they collide or land at the wrong height.
# See ./README.md for what renders on the grid today vs. through the shaper.

reset="\e[0m"
title="\e[1m\e[97m"
rom="\e[2m\e[37m"

echo ""
echo -e "${title}=== HarfBuzz · Thai — stacked vowel + tone marks ===${reset}"
echo ""

echo -e "${rom}\"hello\" — sawatdi${reset}"
echo    "    สวัสดี"
echo ""

echo -e "${rom}\"the Thai language\" — phasa thai${reset}"
echo    "    ภาษาไทย"
echo ""

echo -e "${rom}\"thank you\" — khopkhun${reset}"
echo    "    ขอบคุณ"
echo ""

echo -e "${title}--- base + upper vowel + tone mark (two marks stacked) ---${reset}"
echo -e "${rom}ko-kai (ก) + sara-i (ิ) + mai-ek (่)${reset}"
echo    "    กิ่"
echo ""

echo -e "${title}--- a run with no word spaces (shaper handles clusters) ---${reset}"
echo    "    ราชการประเทศไทย"
echo ""

echo -e "${title}=== end ===${reset}"
