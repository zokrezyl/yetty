#!/bin/bash
# all.sh — run the full demo set: the scrolling demos first, then the
# interactive ones. Just calls the two enumerated scripts.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/all.sh
#
# To pick demos, edit the `run` lines in all-scrolling.sh / all-interactive.sh
# (comment a line out to skip it). Knob: ALL_CAP=<seconds> per-demo timeout.

set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

bash "$DIR/all-scrolling.sh"
bash "$DIR/all-interactive.sh"
