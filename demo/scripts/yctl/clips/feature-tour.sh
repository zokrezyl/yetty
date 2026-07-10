#!/bin/bash
# Records the "feature tour" clip: every Yetty figure, in one unbroken
# scrollback.
#
#   ./demo/scripts/yctl/clips/feature-tour.sh
#   # -> tmp/clips/feature-tour/feature-tour.mp4
#
# Plays the scenario demo/assets/yctl/clips/feature-tour.yaml against a
# dedicated --record yetty. Stages each asset the scenario references into the
# recording cwd under the clean name it types (so `ycat report.pdf` etc. stay
# path-free on screen) and puts the demo tools on PATH via _record-common.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/feature-tour.yaml"
STAGING_DIR="$ROOT/tmp/clips/feature-tour"

mkdir -p "$STAGING_DIR"
# Stage each asset under the clean name the scenario types.
cp "$ROOT/demo/assets/yimage/wordmark.png"          "$STAGING_DIR/wordmark.png"
cp "$ROOT/demo/assets/ychart/revenue.chart"         "$STAGING_DIR/revenue.chart"
cp "$ROOT/demo/assets/ydiagram/sequence-diagram.mmd" "$STAGING_DIR/sequence-diagram.mmd"
cp "$ROOT/demo/assets/ymarkdown/overview.md"        "$STAGING_DIR/overview.md"
cp "$ROOT/demo/assets/ypdf/report.pdf"              "$STAGING_DIR/report.pdf"
cp "$ROOT/demo/assets/yimage/hero.png"              "$STAGING_DIR/hero.png"
cp "$ROOT/demo/assets/svg/tiger.svg"                "$STAGING_DIR/tiger.svg"
cp "$ROOT/demo/assets/music/ode-to-joy.ly"          "$STAGING_DIR/ode-to-joy.ly"
cp "$ROOT/demo/assets/ycircuit/555-blinker.circuit" "$STAGING_DIR/555-blinker.circuit"
cp "$ROOT/demo/assets/yflame/profile.folded"        "$STAGING_DIR/profile.folded"
cp "$ROOT/demo/assets/ymesh/Duck.glb"               "$STAGING_DIR/Duck.glb"
cp "$ROOT/demo/assets/yshadertoy/swirl.wgsl"        "$STAGING_DIR/swirl.wgsl"
cp "$ROOT/demo/assets/yvideo/testsrc.h264"          "$STAGING_DIR/testsrc.h264"

record_clip "$SCENARIO" "$STAGING_DIR" "feature-tour.mp4"
