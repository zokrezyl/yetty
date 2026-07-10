#!/bin/bash
# Records the "Shader in scrollback" clip.
#
#   ./demo/scripts/yctl/clips/shader-in-scrollback.sh
#   # -> tmp/clips/shader-in-scrollback/shader-in-scrollback.mp4

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/shader-in-scrollback.yaml"
STAGING_DIR="$ROOT/tmp/clips/shader-in-scrollback"

mkdir -p "$STAGING_DIR"
cp "$ROOT/demo/assets/yshadertoy/plasma.wgsl" "$STAGING_DIR/plasma.wgsl"
cp "$ROOT/demo/assets/yshadertoy/swirl.wgsl" "$STAGING_DIR/swirl.wgsl"

record_clip "$SCENARIO" "$STAGING_DIR" "shader-in-scrollback.mp4"
