#!/bin/bash
# HarfBuzz demo — Tamil: two-part (split) vowel signs.
#
# Several Tamil vowel signs (ொ ோ ௌ) are typed as one codepoint but render as
# two glyphs that sit on BOTH sides of the consonant — the shaper splits and
# places them. Tamil also reorders pre-base signs like ெ ே ை.
# See ./README.md for what renders on the grid today vs. through the shaper.

reset="\e[0m"
title="\e[1m\e[97m"
rom="\e[2m\e[37m"

echo ""
echo -e "${title}=== HarfBuzz · Tamil — split + reordered vowel signs ===${reset}"
echo ""

echo -e "${rom}\"Tamil\" — tamizh${reset}"
echo    "    தமிழ்"
echo ""

echo -e "${rom}\"greetings\" — vanakkam${reset}"
echo    "    வணக்கம்"
echo ""

echo -e "${rom}\"world\" — ulagam${reset}"
echo    "    உலகம்"
echo ""

echo -e "${title}--- split vowel sign wrapping the consonant ---${reset}"
echo -e "${rom}ka (க) + o-sign (ொ)  ->  one glyph before, one after${reset}"
echo    "    கொ"
echo ""

echo -e "${title}--- pre-base vowel sign ---${reset}"
echo -e "${rom}ka (க) + e-sign (ெ)${reset}"
echo    "    கெ"
echo ""

echo -e "${title}=== end ===${reset}"
