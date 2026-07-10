#!/bin/bash
# Records the "plot with logs" clip.
#
#   ./demo/scripts/yctl/clips/plot-with-logs.sh
#   # -> tmp/clips/plot-with-logs/plot-with-logs.mp4

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/plot-with-logs.yaml"
STAGING_DIR="$ROOT/tmp/clips/plot-with-logs"

mkdir -p "$STAGING_DIR"
cp "$ROOT/demo/assets/yctl/clips/plot-with-logs.sh" "$STAGING_DIR/plot-with-logs.sh"
chmod +x "$STAGING_DIR/plot-with-logs.sh"

record_clip "$SCENARIO" "$STAGING_DIR" "plot-with-logs.mp4"
