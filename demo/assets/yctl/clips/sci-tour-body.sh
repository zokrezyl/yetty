#!/bin/bash
# sci-tour-body.sh — the self-driving content of the "sci tour" clip:
# yetty for the scientific community, every act on real data.
#
# One pass over a computational scientist's day, in one scrollback:
#   1  a real solver streaming its observable LIVE into a scrolling plot
#   2  the same solver's computed orbit as a figure + its convergence
#      story on a log axis
#   3  wave physics as an animated colormapped GPU field with a colorbar
#   4  the climate record (Keeling curve, GISTEMP)
#   5  GW150914 strain vs template straight from the GWOSC data files,
#      then the discovery paper inline
#   6  a month of global seismicity — baked over NASA satellite tiles
#   7  typeset mathematics (ymath)
#   8  molecules and point clouds in 3D (ymesh: GLB + PLY)
#   9  a live N-body galaxy through the yrdawn bridge
#  10  matplotlib figures inline (the Python story)
#  11  pipeline DAG, solver profile, markdown lab notebook
#
# Staged into the recording directory and typed as `./sci-tour.sh` by
# demo/assets/yctl/clips/sci-tour.yaml. Preview directly:
#
#   YETTY_REPO=$PWD ./demo/assets/yctl/clips/sci-tour-body.sh
#
# Environment (exported by demo/scripts/yctl/clips/sci-tour.sh):
#   YETTY_REPO        repo root — used to find demo/assets
#   YETTY_BUILD_DIR   build tree with the tools (default: <repo>/build-desktop-ytrace-release)
#   HOLD              seconds each figure holds on screen (default: 1.6)
#
# Every step is best-effort: a missing tool, asset or network resource
# prints a note and the tour moves on, so one gap never aborts the parade.

REPO="${YETTY_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
BUILD="${YETTY_BUILD_DIR:-$REPO/build-desktop-ytrace-release}"
ASSETS="$REPO/demo/assets"
SCI="$ASSETS/yscience"
HOLD="${HOLD:-1.6}"

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
YPLOT_STREAM="$(tool yplot-stream yplot-stream)"
YMATH="$(tool ymath ymath)"
YMAP="$(tool ymap ymap)"
YDIAGRAM="$(tool ydiagram ydiagram)"
YFLAME="$(tool yflame yflame)"
YMESH="$(tool ymesh ymesh)"
NBODY="$BUILD/demo/yrdawn/12_nbody/demo-yrdawn-12-nbody"

FIG_W=1500
FIG_H=640
CELL_W=165

sec() {
    printf '\n\033[1;38;2;107;168;146m▊ %s\033[0m\n\n' "$1"
    sleep 0.5
}
hold() { sleep "$HOLD"; }
cap() { timeout -k 1 "$1" "${@:2}" 2>/dev/null || true; }
have() { [ -n "$1" ] && [ -x "$1" ]; }

# ── 0. cold open ─────────────────────────────────────────────────────────
if have "$YCAT" && [ -f "$ASSETS/yimage/wordmark.png" ]; then
    "$YCAT" -w "$CELL_W" "$ASSETS/yimage/wordmark.png"
    sleep 1.0
fi
printf '\033[38;2;159;167;168myetty for science — simulations, live data and papers, one scrollback.\033[0m\n'
hold

# ── 1. a real solver, streaming LIVE ─────────────────────────────────────
sec "live — the three-body solver streaming an observable into a scrolling plot"
if command -v python3 >/dev/null && have "$YPLOT_STREAM" && [ -f "$SCI/solver-threebody.py" ]; then
    # No timeout wrapper: the solver's --stream budget self-terminates the
    # pipeline (EOF → yplot-stream exits). A timeout here can orphan the
    # pipeline (timeout signals only its direct child), leaving a zombie
    # producer whose CMD_UPDATE envelopes retarget whatever figure claims
    # the stream id next — that corrupts later acts.
    python3 "$SCI/solver-threebody.py" --stream 420 | \
        "$YPLOT_STREAM" --len=280 --yrange=0..4 \
        --title='body 1 - body 2 separation (live)' || true
fi
hold

# ── 2. the computed orbit + the convergence story ────────────────────────
sec "run — one full figure-8 period, RK4, energy conserved to 1e-15"
if command -v python3 >/dev/null && [ -f "$SCI/solver-threebody.py" ]; then
    python3 "$SCI/solver-threebody.py" threebody-orbit.json
fi
hold

sec "scales — log-axis residual decay, straight line = exponential (yplot)"
if have "$YPLOT"; then
    "$YPLOT" -w "$FIG_W" -H 560 --ylog --yrange=0.000001..1 --xrange=0..30 \
        --title 'iterative solver convergence' --xlabel 'iteration' --ylabel 'residual' \
        'jacobi=exp(0-0.25*x); gauss_seidel=exp(0-0.5*x); multigrid=exp(0-1.5*x)' \
        '@jacobi.color=#FF6B6B' '@gauss_seidel.color=#FFE66D' '@multigrid.color=#6BA892'
fi
hold

# ── 3. wave physics on the GPU ───────────────────────────────────────────
sec "field — two-source interference, f(x,y) live on the GPU, magma + colorbar"
if have "$YPLOT"; then
    "$YPLOT" -w "$FIG_W" -H "$FIG_H" --xrange=-6.28..6.28 --yrange=-3.14..3.14 \
        --colormap magma --field-range=-1.6..1.6 \
        --title 'psi(x,y,t) - two coherent sources' --xlabel 'x' \
        'psi=sin(8*sqrt((x-1.5)*(x-1.5)+y*y)-2*time)/(1+sqrt((x-1.5)*(x-1.5)+y*y))+sin(8*sqrt((x+1.5)*(x+1.5)+y*y)-2*time)/(1+sqrt((x+1.5)*(x+1.5)+y*y))'
fi
hold

# ── 4. the climate record ────────────────────────────────────────────────
sec "data — sixty-seven years of CO2: the Keeling curve (NOAA GML)"
if have "$YPLOT" && [ -f co2_mm_mlo.csv ]; then
    "$YPLOT" -w "$FIG_W" -H "$FIG_H" \
        --title 'atmospheric CO2 at Mauna Loa - the Keeling curve' \
        --xlabel 'months since 1958-03' --ylabel 'CO2 (ppm)' \
        --data 'monthly mean=co2_mm_mlo.csv:average'
fi
hold
sec "data — 145 years of warming (NASA GISTEMP)"
if have "$YPLOT" && [ -f "$SCI/gistemp-annual.txt" ]; then
    "$YPLOT" -w "$FIG_W" -H 520 \
        --title 'global mean temperature anomaly vs 1951-1980' \
        --xlabel 'years since 1880' --ylabel 'anomaly (deg C)' \
        --data "annual mean=$SCI/gistemp-annual.txt:1"
fi
hold

# ── 5. the first gravitational wave ──────────────────────────────────────
sec "signal — GW150914: H1 strain vs numerical relativity, from the GWOSC files"
if have "$YPLOT" && [ -f gw-observed-H.txt ] && [ -f gw-template-H.txt ]; then
    "$YPLOT" -w "$FIG_W" -H "$FIG_H" \
        --title 'GW150914 - the first gravitational-wave detection' \
        --xlabel 'sample (16 kHz)' --ylabel 'strain (1e-21)' --legend \
        --data 'H1 observed=gw-observed-H.txt:1' \
        --data 'NR template=gw-template-H.txt:1'
elif have "$YCHART"; then
    "$YCHART" --width "$FIG_W" --height "$FIG_H" "$SCI/gw150914.json"
fi
hold
if have "$YCAT" && [ -f gw150914-paper.pdf ]; then
    sec "paper — the discovery paper, PRL 116, 061102 (arXiv:1602.03837)"
    "$YCAT" -c pdf gw150914-paper.pdf
    sleep 2.0
fi

# ── 6. global seismicity on satellite imagery ────────────────────────────
sec "earth — a month of M>=4.5 quakes baked over NASA Blue Marble (ymap)"
if have "$YMAP" && [ -f quakes-45-month.geojson ]; then
    cap 60 "$YMAP" -P gibs-bluemarble --lat 0 --lon 140 -z 3 -w 130 -H 40 \
        --geojson quakes-45-month.geojson
elif have "$YCHART"; then
    "$YCHART" --width "$FIG_W" --height "$FIG_H" "$SCI/quakes-world.json"
fi
hold

# ── 7. typeset mathematics ───────────────────────────────────────────────
sec "math — the equations behind the acts, typeset inline (ymath)"
if have "$YMATH"; then
    "$YMATH" --size=32 '\frac{\partial u}{\partial t} = \alpha \nabla^2 u'
    echo
    "$YMATH" --size=32 '\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6}'
    echo
    "$YMATH" --size=32 'x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}'
    echo
fi
hold

# ── 8. molecules and point clouds ────────────────────────────────────────
sec "molecule — caffeine from PubChem, ball-and-stick (ymesh, GLB)"
have "$YMESH" && cap 8 "$YMESH" --once -w 900 -H "$FIG_H" "$SCI/caffeine.glb"
hold
if [ -f helix.ply ]; then
    sec "point cloud — 2000 samples, vertex-only PLY (ymesh)"
    have "$YMESH" && cap 8 "$YMESH" --once -w 700 -H "$FIG_H" helix.ply
    hold
fi

# ── 9. a live N-body galaxy through the yrdawn bridge ────────────────────
if [ -x "$NBODY" ]; then
    sec "gravity — 480 bodies, leapfrog, streamed live through the yrdawn bridge"
    # The demo ends on its own (~12 s) and tears its canvas down
    # (DELETE_CHILD) on the way out — including on SIGTERM, so the
    # safety timeout below can't leave the figure orphaned on screen.
    # --foreground: the bridge client reads its responses from the tty;
    # in timeout's default (background) process group that read raises
    # SIGTTIN and the demo never draws.
    timeout -k 2 --foreground 20 "$NBODY" 2>/dev/null || true
    hold
fi

# ── 10. the Python story ─────────────────────────────────────────────────
if [ -x "$REPO/demo/python/mpl-demo.py" ] && command -v uv >/dev/null; then
    sec "python — matplotlib figures, rendered inline (yetty.mpl)"
    cap 120 "$REPO/demo/python/mpl-demo.py"
    hold
fi

# ── 11. pipeline, profile, notebook ──────────────────────────────────────
sec "pipeline — strain to posterior, as a DAG (ydiagram)"
have "$YDIAGRAM" && "$YDIAGRAM" "$SCI/pipeline.mmd"
hold

sec "profile — where the solver spends its time (yflame)"
have "$YFLAME" && [ -f "$ASSETS/yflame/profile.folded" ] && \
    "$YFLAME" -w "$FIG_W" "$ASSETS/yflame/profile.folded"
hold

sec "notebook — the day's results, rendered markdown (ycat)"
have "$YCAT" && "$YCAT" -w 130 -c markdown "$SCI/sci-report.md"
hold

# ── close ────────────────────────────────────────────────────────────────
printf '\n\033[1;38;2;107;168;146m▊ that was one scrollback.\033[0m '
printf '\033[38;2;159;167;168mthe experiment, the live data, the paper — together.\033[0m\n\n'
sleep 1.5
