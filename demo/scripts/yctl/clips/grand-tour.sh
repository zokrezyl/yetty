#!/bin/bash
# Records the "grand tour" clip: one pass over Yetty's whole rich-content
# surface in a single scrollback.
#
#   ./demo/scripts/yctl/clips/grand-tour.sh
#   # -> tmp/clips/grand-tour/grand-tour.mp4
#
# Staged body: demo/assets/yctl/clips/grand-tour-body.sh (copied in as
# ./grand-tour.sh so the typed command stays clean). The body resolves the
# demo tools and assets via YETTY_REPO / YETTY_BUILD_DIR, exported below so
# the recording instance's shell inherits them.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/grand-tour.yaml"
STAGING_DIR="$ROOT/tmp/clips/grand-tour"
BODY="$ROOT/demo/assets/yctl/clips/grand-tour-body.sh"

if [ ! -f "$BODY" ]; then
    echo "grand tour body not found: $BODY" >&2
    exit 1
fi

mkdir -p "$STAGING_DIR"
cp "$BODY" "$STAGING_DIR/grand-tour.sh"
chmod +x "$STAGING_DIR/grand-tour.sh"

# The staged tour reaches back into the repo for the demo assets, tools and
# effects.sh. record_clip preserves the environment (it only overrides PATH),
# so the recording shell inherits these.
export YETTY_REPO="$ROOT"
export YETTY_BUILD_DIR="$BUILD_DIR"

record_clip "$SCENARIO" "$STAGING_DIR" "grand-tour.mp4"
