#!/bin/bash
# Test script to trigger moverect callback
# This tests that plugin grid markers are updated correctly
# when content is moved within the visible screen area.

# CSI escape sequence helpers
CSI=$'\033['

# Clear screen and move to top
echo -e "${CSI}2J${CSI}H"

echo "=== Moverect Test ==="
echo ""
echo "This test uses Insert Line (IL) and Delete Line (DL)"
echo "to trigger the moverect callback."
echo ""

# Print some lines with emojis that will be tracked
echo "Line 1: Static header"
echo "Line 2: 🚀 Rocket emoji - should move with scroll"
echo "Line 3: ❤️ Heart emoji - should move with scroll"
echo "Line 4: ⭐ Star emoji - should move with scroll"
echo "Line 5: 🔥 Fire emoji - should move with scroll"
echo "Line 6: Static footer"
echo ""
echo "Press Enter to INSERT a line at row 3 (content moves DOWN)"
read

# Move to row 3 and insert a line (CSI L = Insert Line)
echo -e "${CSI}3;1H${CSI}L>>> INSERTED LINE <<<"

echo -e "${CSI}10;1H"
echo "Line inserted! Emojis should have moved down by 1 row."
echo "Press Enter to DELETE a line at row 3 (content moves UP)"
read

# Move to row 3 and delete a line (CSI M = Delete Line)
echo -e "${CSI}3;1H${CSI}M"

echo -e "${CSI}10;1H"
echo "Line deleted! Emojis should have moved back up."
echo ""
echo "Press Enter for scroll region test..."
read

# Clear and setup scroll region test
echo -e "${CSI}2J${CSI}H"
echo "=== Scroll Region Test ==="
echo ""
echo "Setting scroll region to rows 5-10"
echo ""
echo "Line 4: OUTSIDE scroll region (top)"
# Set scroll region (DECSTBM) rows 5-10
echo -e "${CSI}5;10r"
# Move into scroll region
echo -e "${CSI}5;1H"
echo "Line 5: 🎉 Party - INSIDE scroll region"
echo "Line 6: 🎊 Confetti - INSIDE scroll region"
echo "Line 7: 🎈 Balloon - INSIDE scroll region"
echo "Line 8: 🎁 Gift - INSIDE scroll region"
echo "Line 9: 🎂 Cake - INSIDE scroll region"

echo -e "${CSI}12;1H"
echo "Line 11: OUTSIDE scroll region (bottom)"
echo ""
echo "Press Enter to scroll UP inside region (CSI S)"
read

# Scroll up 2 lines inside region
echo -e "${CSI}5;1H${CSI}2S"

echo -e "${CSI}14;1H"
echo "Scrolled up! Content in region moved up by 2."
echo "Press Enter to scroll DOWN inside region (CSI T)"
read

# Scroll down 1 line inside region
echo -e "${CSI}5;1H${CSI}1T"

echo -e "${CSI}16;1H"
echo "Scrolled down! Content in region moved down by 1."
echo ""

# Reset scroll region
echo -e "${CSI}r"
echo "Press Enter to exit..."
read

echo -e "${CSI}2J${CSI}H"
echo "Test complete!"
