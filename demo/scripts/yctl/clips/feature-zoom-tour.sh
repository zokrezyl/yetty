#!/bin/bash
# Records the "feature zoom tour" clip: each figure zoomed to fill the frame.
#
#   ./demo/scripts/yctl/clips/feature-zoom-tour.sh
#   # -> tmp/clips/feature-zoom-tour/feature-zoom-tour.mp4
#
# Stages every asset the scenario references into the recording cwd (so the
# typed commands stay clean, e.g. `ycat report.pdf`) and puts the demo tools
# on PATH via _record-common.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/feature-zoom-tour.yaml"
STAGING_DIR="$ROOT/tmp/clips/feature-zoom-tour"

mkdir -p "$STAGING_DIR"
# Stage each asset under the clean name the scenario types.
cp "$ROOT/demo/assets/yimage/wordmark.png"          "$STAGING_DIR/wordmark.png"
cp "$ROOT/demo/assets/ychart/browsers.json"         "$STAGING_DIR/browsers.json"
cp "$ROOT/demo/assets/ydiagram/directions.mmd"      "$STAGING_DIR/directions.mmd"
cp "$ROOT/demo/assets/ymarkdown/overview.md"        "$STAGING_DIR/overview.md"
cp "$ROOT/demo/assets/ypdf/report.pdf"              "$STAGING_DIR/report.pdf"
cp "$ROOT/demo/assets/yimage/rose.png"              "$STAGING_DIR/rose.png"
cp "$ROOT/demo/assets/svg/tiger.svg"                "$STAGING_DIR/tiger.svg"
cp "$ROOT/demo/assets/music/ode-to-joy.ly"          "$STAGING_DIR/ode-to-joy.ly"
cp "$ROOT/demo/assets/ycircuit/555-blinker.circuit" "$STAGING_DIR/555-blinker.circuit"
cp "$ROOT/demo/assets/yflame/profile.folded"        "$STAGING_DIR/profile.folded"
cp "$ROOT/demo/assets/ymesh/Duck.glb"               "$STAGING_DIR/Duck.glb"
cp "$ROOT/demo/assets/yshadertoy/plasma.wgsl"       "$STAGING_DIR/plasma.wgsl"

record_clip "$SCENARIO" "$STAGING_DIR" "feature-zoom-tour.mp4"
