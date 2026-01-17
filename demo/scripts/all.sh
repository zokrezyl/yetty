#!/bin/bash
# Run all yetty demo scripts automatically
# Discovers and runs all .sh files in demo/scripts and subdirectories
# Usage: ./demo/scripts/all.sh

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# Colors for output
CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Sleep duration between demos (seconds)
SLEEP_DURATION=5

# Extract description from script (first comment line after shebang)
get_description() {
    local script="$1"
    # Get the first comment line (starting with #) after the shebang
    sed -n '2p' "$script" | sed 's/^# *//'
}

# Run a single demo
run_demo() {
    local script="$1"
    local relative_path="${script#$DIR/}"
    local description
    description=$(get_description "$script")

    echo ""
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}Demo: ${MAGENTA}$relative_path${NC}"
    echo -e "${YELLOW}$description${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo ""

    bash "$script" || echo -e "${YELLOW}Warning: $relative_path exited with non-zero status${NC}"

    echo ""
    echo -e "${CYAN}Waiting ${SLEEP_DURATION}s before next demo...${NC}"
    sleep "$SLEEP_DURATION"
}

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║              YETTY WIDGET DEMO SHOWCASE                      ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "This demo runs all widget demos from demo/scripts/"
echo "Each demo creates widgets that remain visible until cleared."
echo "Press Ctrl+C to stop at any time."
echo ""
echo -e "${YELLOW}Sleep between demos: ${SLEEP_DURATION} seconds${NC}"
echo ""
sleep 2

# Find all demo scripts (excluding this file)
SCRIPT_NAME=$(basename "$0")
DEMO_SCRIPTS=$(find "$DIR" -name "*.sh" -type f ! -name "$SCRIPT_NAME" | sort)

# Count demos
DEMO_COUNT=$(echo "$DEMO_SCRIPTS" | wc -l)
CURRENT=0

echo -e "${CYAN}Found $DEMO_COUNT demo scripts${NC}"
echo ""

# Run each demo
while IFS= read -r script; do
    CURRENT=$((CURRENT + 1))
    echo -e "${MAGENTA}[$CURRENT/$DEMO_COUNT]${NC}"
    run_demo "$script"
done <<< "$DEMO_SCRIPTS"

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                    DEMO COMPLETE                             ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "All $DEMO_COUNT demos have been executed."
echo "Widgets remain visible until cleared."
echo ""
echo "Commands:"
echo "  yetty-client ls         - List active widgets"
echo "  yetty-client rm <id>    - Remove a specific widget"
echo "  yetty-client clear      - Remove all widgets"
echo ""
