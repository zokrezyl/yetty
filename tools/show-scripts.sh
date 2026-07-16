#!/usr/bin/env bash
# Multi-script + emoji rendering smoke.
#
# Prints UDHR Article-1 sample lines for every script the bundled Noto fallback
# chain is expected to cover, plus emoji test lines (single, VS16, ZWJ, flags,
# skin tones) and a notdef/tofu probe. Eyeball the output in a live yetty: every
# script should show real glyphs, emoji should render in color, and the only
# boxes should be on the intentionally-unassigned codepoints in the last line.
#
# Sibling of show-cjk.sh (which sweeps raw codepoint ranges); this one uses real
# sentences so the fallback chain, shaping gaps and color path are all visible.

printf '%-12s %s\n' "Latin:"      "All human beings are born free and equal"
printf '%-12s %s\n' "Cyrillic:"   "Все люди рождаются свободными и равными"
printf '%-12s %s\n' "Greek:"      "Όλοι οι άνθρωποι γεννιούνται ελεύθεροι"
printf '%-12s %s\n' "Armenian:"   "Բոլոր մարդիկ ծնվում են ազատ"
printf '%-12s %s\n' "Georgian:"   "ყველა ადამიანი იბადება თავისუფალი"
printf '%-12s %s\n' "CJK (SC):"   "人人生而自由，在尊严和权利上一律平等。"
printf '%-12s %s\n' "CJK (JP):"   "すべての人間は、生まれながらにして自由であり"
printf '%-12s %s\n' "Hangul:"     "모든 인간은 태어날 때부터 자유로우며"
printf '%-12s %s\n' "Devanagari:" "सभी मनुष्यों को गौरव और अधिकारों में जन्मजात"
printf '%-12s %s\n' "Bengali:"    "সমস্ত মানুষ স্বাধীনভাবে সমান মর্যাদা"
printf '%-12s %s\n' "Tamil:"      "மனிதப் பிறவியினர் அனைவரும் சுதந்திரமாகவே"
printf '%-12s %s\n' "Thai:"       "มนุษย์ทั้งหลายเกิดมามีอิสระและเสมอภาคกัน"
printf '%-12s %s\n' "Arabic:"     "يولد جميع الناس أحرارًا متساوين في الكرامة"
printf '%-12s %s\n' "Hebrew:"     "כל בני האדם נולדו בני חורין ושווים"
printf '%-12s %s\n' "Emoji:"      "😀 🎉 🚀 ❤️ 👍 🌍 🔥 ⭐ 🍕 🎨"
printf '%-12s %s\n' "VS16 pairs:" "☺️ ✌️ ☀️ ♻️ ✈️"
printf '%-12s %s\n' "ZWJ seqs:"   "👨‍👩‍👧 👩‍💻 🧑‍🚀 🏳️‍🌈"
printf '%-12s %s\n' "Flags/skin:" "🇺🇸 🇯🇵 🇪🇺 👋🏽 ✌🏿 🤙🏻"
# The last three are intentionally unassigned/uncoverable — they must render as
# a visible notdef (tofu) box, never blank and never a crash.
printf '%-12s %s\n' "Notdef:"     $'[\U0002FFFF] [\U0003FFFF] [\U000ABCDE]'
