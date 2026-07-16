#!/bin/bash
# HarfBuzz demo — Devanagari (Hindi / Sanskrit): reordering and conjuncts.
#
# Two behaviours per-codepoint lookup cannot do:
#   - The pre-base i-matra (ि, U+093F) is typed AFTER its consonant but drawn
#     BEFORE it — the shaper reorders the run.
#   - A consonant + virama (्) + consonant collapses into one conjunct ligature.
# Without shaping the matra sits on the wrong side and conjuncts stay split.
# See ./README.md for what renders on the grid today vs. through the shaper.

reset="\e[0m"
title="\e[1m\e[97m"
rom="\e[2m\e[37m"

echo ""
echo -e "${title}=== HarfBuzz · Devanagari — reordering + conjuncts ===${reset}"
echo ""

echo -e "${rom}\"greetings\" — namaste${reset}"
echo    "    नमस्ते"
echo ""

echo -e "${rom}\"Hindi\" — hindi (note the न्द conjunct)${reset}"
echo    "    हिन्दी"
echo ""

echo -e "${rom}\"Sanskrit\" — sanskrit${reset}"
echo    "    संस्कृतम्"
echo ""

echo -e "${title}--- pre-base i-matra reordering (typed ka + i) ---${reset}"
echo -e "${rom}ka (क) + i-sign (ि)  ->  the sign is drawn to the LEFT of ka${reset}"
echo    "    कि"
echo ""

echo -e "${title}--- classic conjuncts ---${reset}"
echo -e "${rom}kSa · tra · jna${reset}"
echo    "    क्ष    त्र    ज्ञ"
echo ""

echo -e "${title}=== end ===${reset}"
