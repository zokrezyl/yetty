#!/bin/bash
# all.sh — run every HarfBuzz complex-script demo in sequence.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/harfbuzz/all.sh
#
# Knob: DEMO_PAUSE=<seconds> pauses between scripts (default 1).

set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PAUSE="${DEMO_PAUSE:-1}"

for script in arabic devanagari bengali tamil thai; do
    bash "$DIR/$script.sh"
    sleep "$PAUSE"
done
