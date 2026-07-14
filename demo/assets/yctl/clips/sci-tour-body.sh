#!/bin/bash
# sci-tour-body.sh — the self-driving content of the "sci tour" clip:
# yetty for the scientific community, every act on real data.
#
# One pass over a computational scientist's day, in one scrollback:
# a real numerical experiment running live (three-body figure-8, RK4,
# with the energy-conservation check in the log), an animated GPU wave
# field, the Keeling curve, the GW150914 strain against its template,
# a month of global seismicity, satellite imagery, a molecule in 3D,
# live wave-optics on the GPU, the analysis pipeline, a profile, and
# the lab notebook.
#
# It is staged into the recording directory and typed as `./sci-tour.sh`
# by demo/assets/yctl/clips/sci-tour.yaml. Run it directly to preview:
#
#   YETTY_REPO=$PWD ./demo/assets/yctl/clips/sci-tour-body.sh
#
# Environment (exported by demo/scripts/yctl/clips/sci-tour.sh):
#   YETTY_REPO        repo root — used to find demo/assets
#   YETTY_BUILD_DIR   build tree with the tools (default: <repo>/build-desktop-ytrace-release)
#   HOLD              seconds each figure holds on screen (default: 1.3)
#
# Every step is best-effort: a missing tool, asset or network resource
# prints a note and the tour moves on, so one gap never aborts the parade.

REPO="${YETTY_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
BUILD="${YETTY_BUILD_DIR:-$REPO/build-desktop-ytrace-release}"
ASSETS="$REPO/demo/assets"
SCI="$ASSETS/yscience"
HOLD="${HOLD:-1.3}"

# Resolve a tool binary: prefer the build tree, fall back to $PATH.
tool() {
    local dir="$1" name="$2"
    if [ -x "$BUILD/tools/$dir/$name" ]; then
        printf '%s' "$BUILD/tools/$dir/$name"
    else
        command -v "$name" 2>/dev/null || true
    fi
}

YCAT="$(tool ycat ycat)"
YCHART="$(tool ychart ychart)"
YPLOT="$(tool yplot yplot)"
YDIAGRAM="$(tool ydiagram ydiagram)"
YFLAME="$(tool yflame yflame)"
YMESH="$(tool ymesh ymesh)"
YMAP="$(tool ymap ymap)"

FIG_W=1500
FIG_H=680
CELL_W=165

# Mint section header (BRAND_ACCENT #6BA892), then a beat.
sec() {
    printf '\n\033[1;38;2;107;168;146m▊ %s\033[0m\n\n' "$1"
    sleep 0.5
}
hold() { sleep "$HOLD"; }

# Run a possibly-blocking player under a hard wall-clock cap.
cap() { timeout -k 1 "$1" "${@:2}" 2>/dev/null || true; }

have() { [ -n "$1" ] && [ -x "$1" ]; }

# ── 0. cold open ─────────────────────────────────────────────────────────
if have "$YCAT" && [ -f "$ASSETS/yimage/wordmark.png" ]; then
    "$YCAT" -w "$CELL_W" "$ASSETS/yimage/wordmark.png"
    sleep 1.0
fi
printf '\033[38;2;159;167;168myetty for science — simulations, data and papers, one scrollback.\033[0m\n'
hold

# ── 1. a real numerical experiment, running live ─────────────────────────
sec "run — three-body figure-8 orbit, RK4, energy check (python + ychart)"
if command -v python3 >/dev/null && [ -f "$SCI/solver-threebody.py" ]; then
    python3 "$SCI/solver-threebody.py" threebody-orbit.json
    sleep 0.6
    if have "$YCHART" && [ -f threebody-orbit.json ]; then
        "$YCHART" --width 1100 --height "$FIG_H" threebody-orbit.json
    fi
fi
hold

# ── 2. wave physics on the GPU ───────────────────────────────────────────
sec "field — interference of two circular waves, f(x,y) live on the GPU (yplot)"
if have "$YPLOT"; then
    "$YPLOT" -w "$FIG_W" -H "$FIG_H" --xrange=-6.28..6.28 --yrange=-3.14..3.14 \
        'psi=sin(8*sqrt((x-1.5)*(x-1.5)+y*y)-2*time)/(1+sqrt((x-1.5)*(x-1.5)+y*y))+sin(8*sqrt((x+1.5)*(x+1.5)+y*y)-2*time)/(1+sqrt((x+1.5)*(x+1.5)+y*y))'
fi
hold

# ── 3. the climate record ────────────────────────────────────────────────
sec "data — sixty-seven years of CO2: the Keeling curve (NOAA GML)"
have "$YCHART" && "$YCHART" --width "$FIG_W" --height "$FIG_H" "$SCI/co2-keeling.json"
hold
sec "data — 145 years of global temperature anomaly (NASA GISTEMP)"
have "$YCHART" && "$YCHART" --width "$FIG_W" --height "$FIG_H" "$SCI/gistemp-anomaly.json"
hold

# ── 4. the first gravitational wave ──────────────────────────────────────
sec "signal — GW150914: observed strain vs numerical-relativity template (GWOSC)"
have "$YCHART" && "$YCHART" --width "$FIG_W" --height "$FIG_H" "$SCI/gw150914.json"
hold
if have "$YCAT" && [ -f gw150914-paper.pdf ]; then
    sec "paper — the discovery paper, PRL 116, 061102 (arXiv:1602.03837)"
    "$YCAT" -c pdf gw150914-paper.pdf
    sleep 2.0
fi

# ── 5. a month of global seismicity ──────────────────────────────────────
sec "catalog — every M>=4.5 earthquake of the last 30 days: plates emerge (USGS)"
have "$YCHART" && "$YCHART" --width "$FIG_W" --height "$FIG_H" "$SCI/quakes-world.json"
hold

# ── 6. satellite imagery ─────────────────────────────────────────────────
sec "earth — Sentinel-2 cloudless over Mount Etna (ymap)"
have "$YMAP" && cap 25 "$YMAP" -P s2cloudless --lat 37.751 --lon 14.994 -z 11 -w 120 -H 36
hold

# ── 7. molecular structure ───────────────────────────────────────────────
sec "molecule — caffeine, PubChem CID 2519, ball-and-stick (ymesh)"
have "$YMESH" && cap 8 "$YMESH" --once -w 900 -H "$FIG_H" "$SCI/caffeine.glb"
hold

# ── 8. wave optics, live ─────────────────────────────────────────────────
sec "live — two-source interference, computed per pixel per frame (shader)"
have "$YCAT" && "$YCAT" -c shadertoy "$SCI/waves.wgsl"
sleep 2.0

# ── 9. the analysis pipeline ─────────────────────────────────────────────
sec "pipeline — strain to posterior, as a DAG (ydiagram)"
have "$YDIAGRAM" && "$YDIAGRAM" "$SCI/pipeline.mmd"
hold

# ── 10. profiling the solver ─────────────────────────────────────────────
sec "profile — where the solver spends its time (yflame)"
have "$YFLAME" && [ -f "$ASSETS/yflame/profile.folded" ] && \
    "$YFLAME" -w "$FIG_W" "$ASSETS/yflame/profile.folded"
hold

# ── 11. the lab notebook ─────────────────────────────────────────────────
sec "notebook — the day's results, rendered markdown (ycat)"
have "$YCAT" && "$YCAT" -w 130 -c markdown "$SCI/sci-report.md"
hold

# ── close ────────────────────────────────────────────────────────────────
printf '\n\033[1;38;2;107;168;146m▊ that was one scrollback.\033[0m '
printf '\033[38;2;159;167;168mthe experiment, the data, the paper — together.\033[0m\n\n'
sleep 1.5
