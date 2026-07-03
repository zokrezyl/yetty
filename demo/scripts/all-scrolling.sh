#!/bin/bash
# all-scrolling.sh — run the scrolling demos (figures that scroll into the
# terminal alongside text). Each demo is one `run` line below: comment a line
# out to skip that demo.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/all-scrolling.sh
#
# ALL_CAP=<seconds>  per-demo hard timeout (default 3)

set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CAP="${ALL_CAP:-3}"

export DEMO_PAUSE=0   # no inter-step pauses inside a demo
export DEMO_HOLD=0    # no end-of-demo "hold open" sleeps

# Run one demo under a wall-clock cap. </dev/null so anything that waits on
# input gets EOF and exits; the timeout is the hard backstop; one demo failing
# never aborts the run. The trailing sleep gives each figure a moment on screen.
run() {
    printf '\n===== %s =====\n' "$1"
    timeout -k 2 "$CAP" bash "$DIR/$1" </dev/null || true
    sleep 0.1
    printf '\nDONE: ===== %s =====\n' "$1"
}

run color-matrix.sh
run standard-colors.sh
run text-color.sh
run text-style.sh

run shader-glyph/demo.sh
run shader-glyph/demo-noclr.sh
run shader-glyph/glyph-list.sh
run shader-glyph/single.sh
run shader-glyph/tile.sh
run shader-glyph/shadertoy/0x00f5-butterfly-flock.sh
run shader-glyph/shadertoy/0x00f6-looping-spline.sh
run shader-glyph/shadertoy/0x00f7-mandelbrot-deco.sh
run shader-glyph/shadertoy/0x00f8-hg-sdf.sh
run shader-glyph/shadertoy/0x00f9-voronoi.sh
run shader-glyph/shadertoy/0x00fa-minkowski-tube.sh
run shader-glyph/shadertoy/0x00fb-biomine.sh
run shader-glyph/shadertoy/0x00fc-julia.sh
run shader-glyph/shadertoy/0x00fd-mandelbrot.sh

run ychart/basic.sh
run ychart/formats.sh
run ychart/gallery.sh



run ycircuit/basic.sh
run ycircuit/symbols.sh

run ydiagram/basic.sh
run ydiagram/gallery.sh

run yecho/text.sh
run yecho/text-sizes.sh
run yecho/plot.sh
run yecho/dashboard.sh

run yflame/basic.sh

run yfspy/basic.sh
run yfspy/signals.sh
run yfspy/procedural.sh
run yfspy/fractals.sh
run yfspy/fractal-zoo.sh
run yfspy/creative.sh

run yimage/basic.sh

run ymap/basic.sh
run ymarkdown/basic.sh
run ymesh/basic.sh
run ymusic/basic.sh
run ypdf/basic.sh

run yplot/basic.sh
run yplot/famous-functions.sh
run yplot/angle-conversion.sh
run yplot/conditionals.sh
run yplot/dashboard.sh
run yplot/domain-and-view.sh
run yplot/error-function.sh
run yplot/heatmaps.sh
run yplot/inverse-hyperbolic.sh
run yplot/signal-shaping.sh
run yplot/sinc.sh
run yplot/smoothstep.sh
run yplot/stochastic.sh
run yplot/trunc.sh
# run yplot/buffer-language.sh   # BROKEN: yfsvm compile "program has no functions"

run yshadertoy/basic.sh
run ysvg/basic.sh
run ysvg/gallery.sh
run ythorvg/basic.sh
run yvideo/basic.sh

# Web pages rendered inline by yetty's two engines (NetSurf + lexbor). The
# local-page renders are offline; each script's live-URL step degrades
# gracefully when there is no network.
run ynetsurf/basic.sh
run ybrowser/basic.sh

# BROKEN — yrich apps fail at construction (yclass dispatch_lookup: slot index
# out of range). Re-enable once the yrich/yclass fix lands.
# run yrich/ydoc/demo.sh
# run yrich/ysheet/demo.sh
# run yrich/yslide/demo.sh

printf '\nall-scrolling.sh: done\n'
