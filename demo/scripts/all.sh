#!/bin/bash
# Run all yetty demo scripts with explanations
# Usage: Run this script inside yetty terminal

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# Colors for output
CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

demo() {
    local script="$1"
    local title="$2"
    local description="$3"

    echo ""
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}$title${NC}"
    echo -e "${YELLOW}$description${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo ""

    bash "$script"
    sleep 1
}

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║              YETTY WIDGET DEMO SHOWCASE                      ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "This demo showcases various widget types supported by yetty."
echo "Each widget will be displayed for viewing. Press Ctrl+C to stop."
echo ""
sleep 2

# Plot widget
demo "plot.sh" \
    "Plot Widget" \
    "Interactive line chart with multiple sine waves. Supports pan (drag) and zoom (scroll)."

# Rich Text
demo "rich-text.sh" \
    "Rich Text Widget" \
    "Styled text rendering with fonts, colors, and formatting from YAML configuration."

# Markdown
demo "markdown.sh" \
    "Markdown Widget" \
    "Renders markdown with headers, bold, italic, code, and lists. Scroll with mouse wheel."

# ThorVG Lottie Animation
demo "thorvg-lottie.sh" \
    "Lottie Animation (ThorVG)" \
    "Animated vector graphics using the Lottie format, rendered via ThorVG."

# ThorVG SVG
demo "thorvg-svg.sh" \
    "SVG Rendering (ThorVG)" \
    "Static SVG vector graphics rendered using the ThorVG library."

# YDraw 2D Showcase
demo "ydraw-showcase.sh" \
    "YDraw 2D Showcase" \
    "2D drawing primitives: circles, rectangles, lines with GPU-accelerated SDF rendering."

# YDraw All Primitives
demo "ydraw-all-primitives.sh" \
    "YDraw All Primitives" \
    "Complete set of 2D SDF primitives including rounded shapes and arcs."

# YDraw 3D Primitives
demo "ydraw-3d-primitives.sh" \
    "YDraw 3D Primitives" \
    "3D SDF primitives: spheres, boxes, cylinders rendered with raymarching."

# YDraw 3D Showcase
demo "ydraw-3d-showcase.sh" \
    "YDraw 3D Showcase" \
    "Advanced 3D scene with lighting, shadows, and multiple objects."

# YDraw Mixed 2D/3D
demo "ydraw-mixed-2d-3d.sh" \
    "YDraw Mixed 2D/3D" \
    "Combined 2D and 3D primitives in a single widget."

# YDraw Widgets
demo "ydraw-widgets.sh" \
    "YDraw UI Widgets" \
    "Interactive UI components: buttons, sliders, progress bars using SDF rendering."

# Python/PyGFX (if available)
if [[ -f "python.sh" ]]; then
    demo "python.sh" \
        "Python Widget (PyGFX)" \
        "Python-powered 3D graphics using pygfx/wgpu for GPU-accelerated rendering."
fi

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                    DEMO COMPLETE                             ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "All demos have been displayed. Widgets remain visible until cleared."
echo "Use 'yetty-client ls' to list active widgets."
echo "Use 'yetty-client rm <id>' to remove a specific widget."
echo ""
