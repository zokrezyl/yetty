#!/bin/bash
# grand-tour-body.sh — the self-driving content of the "grand tour" clip.
#
# One pass over Yetty's whole rich-content surface: styled text, charts,
# plots, diagrams, markdown, a PDF, images, SVG, sheet music, a circuit,
# a flame graph, a 3D mesh, a live GPU shader, vector/Lottie animation,
# and video — every figure landing in the same scrollback as the prompt
# text.
#
# (Full-screen post-processing effects, effects.sh, are deliberately NOT
# part of this tour: the OSC effect path did not render through the
# recording launch and needs its own verified clip. Add it back here only
# once it is confirmed to render.)
#
# It is staged into the recording directory and typed as `./grand-tour.sh`
# by demo/assets/yctl/clips/grand-tour.yaml. It is NOT meant to be fast:
# each figure holds on screen so the recording reads. Run it directly to
# preview:
#
#   YETTY_REPO=$PWD ./demo/assets/yctl/clips/grand-tour-body.sh
#
# Environment (exported by the wrapper demo/scripts/yctl/clips/grand-tour.sh):
#   YETTY_REPO        repo root — used to find demo/assets and effects.sh
#   YETTY_BUILD_DIR   build tree with the tools (default: <repo>/build-desktop-ytrace-release)
#   HOLD              seconds each figure holds on screen (default: 1.3)
#
# Every step is best-effort: a missing tool or asset prints a note and the
# tour moves on, so one gap never aborts the parade.

REPO="${YETTY_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
BUILD="${YETTY_BUILD_DIR:-$REPO/build-desktop-ytrace-release}"
ASSETS="$REPO/demo/assets"
HOLD="${HOLD:-1.3}"

# Resolve a tool binary: prefer the build tree, fall back to $PATH. Echoes
# the resolved path, or empty if not found.
tool() {
    local dir="$1" name="$2"
    if [ -x "$BUILD/tools/$dir/$name" ]; then
        printf '%s' "$BUILD/tools/$dir/$name"
    else
        command -v "$name" 2>/dev/null || true
    fi
}

YCAT="$(tool ycat ycat)"
YECHO="$(tool yecho yecho)"
YPLOT="$(tool yplot yplot)"
YCHART="$(tool ychart ychart)"
YDIAGRAM="$(tool ydiagram ydiagram)"
YFLAME="$(tool yflame yflame)"
YMESH="$(tool ymesh ymesh)"
YTHORVG="$(tool ythorvg yetty-ythorvg)"
YVIDEO="$(tool yvideo yvideo)"

# Figures render at native size (the Ctrl+scroll zoom is not used - it does
# not function in the recording build). So every figure is sized large via
# its own width/height flags to fill the frame instead of leaving a black
# void. FIG_W is the target figure width in pixels for the tools that take
# pixels; ycat takes width in CELLS, so it gets its own CELL_W.
FIG_W=1500
FIG_H=720
CELL_W=165

# Mint section header (BRAND_ACCENT #6BA892), then a beat.
sec() {
    printf '\n\033[1;38;2;107;168;146m▊ %s\033[0m\n\n' "$1"
    sleep 0.5
}
hold() { sleep "$HOLD"; }

# Run a possibly-blocking player under a hard wall-clock cap so the tour
# never hangs on a looping tool.
cap() { timeout -k 1 "$1" "${@:2}" 2>/dev/null || true; }

# Skip a step cleanly when its tool or asset is unavailable.
have() { [ -n "$1" ] && [ -x "$1" ]; }
asset() { [ -f "$ASSETS/$1" ]; }

# ── 0. cold open: the wordmark ──────────────────────────────────────────
if have "$YCAT" && asset yimage/wordmark.png; then
    "$YCAT" -w "$CELL_W" "$ASSETS/yimage/wordmark.png"
    sleep 1.2
fi
printf '\033[38;2;159;167;168mone terminal. text and rich content in the same scrollback.\033[0m\n'
hold

# ── 1. styled text ──────────────────────────────────────────────────────
sec "styled text — color, background, inline glyphs (yecho)"
if have "$YECHO"; then
    "$YECHO" '{color=#6BA892; style=bold: Yetty} {color=#9FA7A8: — terminal unchained}  @rocket'
    "$YECHO" '{color=#FF6B6B: red} {color=#FFE66D: amber} {color=#4ECDC4: cyan} {bg=#1E262C: on a raised row}'
    "$YECHO" 'status: @heart healthy  @spinner working  @gem ready  @signal-bars online'
fi
hold

# ── 2. charts ───────────────────────────────────────────────────────────
sec "charts — column, pie, from .chart/.json (ychart)"
if have "$YCHART"; then
    asset ychart/revenue.chart  && "$YCHART" --width "$FIG_W" --height "$FIG_H" "$ASSETS/ychart/revenue.chart"
    hold
    asset ychart/browsers.json  && "$YCHART" --width "$FIG_W" --height "$FIG_H" "$ASSETS/ychart/browsers.json"
elif have "$YCAT"; then
    asset ychart/revenue.chart  && "$YCAT" "$ASSETS/ychart/revenue.chart"
    hold
    asset ychart/browsers.json  && "$YCAT" "$ASSETS/ychart/browsers.json"
fi
hold

# ── 3. plots ────────────────────────────────────────────────────────────
sec "plots — GPU function plots, live dashboard (yplot)"
if have "$YPLOT"; then
    printf 'CPU / memory / swap, normalized:\n'
    "$YPLOT" -w "$FIG_W" -H "$FIG_H" --xrange=0..6.28 --yrange=0..1 --no-labels \
        'cpu=0.5+0.3*sin(x)+0.1*sin(3*x)' \
        'mem=0.6+0.2*sin(x/2)' \
        'swap=0.1+0.05*sin(x*4)' \
        '@cpu.color=#FF6B6B' '@mem.color=#4ECDC4' '@swap.color=#AA96DA'
fi
hold

# ── 4. diagrams ─────────────────────────────────────────────────────────
sec "diagrams — flowchart & sequence from Mermaid (ydiagram)"
if have "$YDIAGRAM"; then
    asset ydiagram/directions.mmd        && "$YDIAGRAM" "$ASSETS/ydiagram/directions.mmd"
    hold
    asset ydiagram/sequence-diagram.mmd  && "$YDIAGRAM" "$ASSETS/ydiagram/sequence-diagram.mmd"
fi
hold

# ── 5. markdown ─────────────────────────────────────────────────────────
sec "markdown — headings, lists, code, rendered inline (ycat)"
have "$YCAT" && asset ymarkdown/overview.md && "$YCAT" -w "$CELL_W" -c markdown "$ASSETS/ymarkdown/overview.md"
hold

# ── 6. PDF ──────────────────────────────────────────────────────────────
sec "PDF — a real multi-page report in scrollback (ycat)"
have "$YCAT" && asset ypdf/report.pdf && "$YCAT" -c pdf "$ASSETS/ypdf/report.pdf"
sleep 2.0

# ── 7. image ────────────────────────────────────────────────────────────
sec "images — PNG/JPEG decoded to the drawing layer (ycat)"
have "$YCAT" && asset yimage/rose.png && "$YCAT" -w "$CELL_W" "$ASSETS/yimage/rose.png"
hold

# ── 8. SVG ──────────────────────────────────────────────────────────────
sec "vector graphics — SVG at any zoom (ycat)"
have "$YCAT" && asset svg/tiger.svg && "$YCAT" -w "$CELL_W" "$ASSETS/svg/tiger.svg"
hold

# ── 9. music ────────────────────────────────────────────────────────────
sec "sheet music — LilyPond engraved inline (ycat)"
have "$YCAT" && asset music/ode-to-joy.ly && "$YCAT" -w "$CELL_W" "$ASSETS/music/ode-to-joy.ly"
hold

# ── 10. circuit ─────────────────────────────────────────────────────────
sec "circuits — schematic from a netlist (ycat)"
have "$YCAT" && asset ycircuit/555-blinker.circuit && "$YCAT" -w "$CELL_W" "$ASSETS/ycircuit/555-blinker.circuit"
hold

# ── 11. flame graph ─────────────────────────────────────────────────────
sec "flame graph — profiles as a figure (yflame)"
have "$YFLAME" && asset yflame/profile.folded && "$YFLAME" -w "$FIG_W" "$ASSETS/yflame/profile.folded"
hold

# ── 12. 3D mesh ─────────────────────────────────────────────────────────
sec "3D — glTF mesh rendered in the terminal (ymesh)"
have "$YMESH" && asset ymesh/Duck.glb && cap 5 "$YMESH" --once -w 900 -H "$FIG_H" "$ASSETS/ymesh/Duck.glb"
hold

# ── 13. live shader ─────────────────────────────────────────────────────
sec "live shaders — animated GPU figure, not a screenshot (ycat)"
have "$YCAT" && asset yshadertoy/plasma.wgsl && "$YCAT" -c shadertoy "$ASSETS/yshadertoy/plasma.wgsl"
sleep 2.0

# ── 14. vector animation ────────────────────────────────────────────────
sec "animation — SVG & Lottie via ThorVG (ythorvg)"
if have "$YTHORVG"; then
    asset thorvg/sunset.svg          && "$YTHORVG" -w "$FIG_W" -h "$FIG_H" "$ASSETS/thorvg/sunset.svg"
    hold
    asset thorvg/confetti.json       && cap 3 "$YTHORVG" -w "$FIG_W" -h "$FIG_H" --lottie --frame 15 "$ASSETS/thorvg/confetti.json"
fi
hold

# ── 15. video ───────────────────────────────────────────────────────────
sec "video — H.264 decoded into scrollback (yvideo)"
have "$YVIDEO" && asset yvideo/testsrc.h264 && cap 3 "$YVIDEO" --no-loop --bounds-w=1200 "$ASSETS/yvideo/testsrc.h264"
hold

# ── close ───────────────────────────────────────────────────────────────
printf '\n\033[1;38;2;107;168;146m▊ that was one scrollback.\033[0m '
printf '\033[38;2;159;167;168mtext + every figure above, together.\033[0m\n\n'
sleep 1.5
