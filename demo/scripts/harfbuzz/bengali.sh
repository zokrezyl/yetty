#!/bin/bash
# HarfBuzz demo — Bengali: reordering, conjuncts, and the reph.
#
# Like Devanagari, Bengali reorders pre-base vowel signs and forms conjuncts
# across the virama (্). A word-initial "ra + virama" becomes a reph that rides
# above a later consonant. None of this happens per-codepoint.
# See ./README.md for what renders on the grid today vs. through the shaper.

reset="\e[0m"
title="\e[1m\e[97m"
rom="\e[2m\e[37m"

echo ""
echo -e "${title}=== HarfBuzz · Bengali — reordering + conjuncts ===${reset}"
echo ""

echo -e "${rom}\"Bengali\" — bangla${reset}"
echo    "    বাংলা"
echo ""

echo -e "${rom}\"greetings\" — nomoshkar${reset}"
echo    "    নমস্কার"
echo ""

echo -e "${rom}\"book\" — boi${reset}"
echo    "    বই"
echo ""

echo -e "${title}--- conjuncts ---${reset}"
echo -e "${rom}kSa · ndhya${reset}"
echo    "    ক্ষ    ন্ধ্য"
echo ""

echo -e "${title}--- pre-base vowel sign (drawn before the consonant) ---${reset}"
echo -e "${rom}ka (ক) + e-sign (ে)${reset}"
echo    "    কে"
echo ""

echo -e "${title}=== end ===${reset}"
