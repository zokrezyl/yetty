#!/bin/bash
# ynotebook — run every ynotebook demo in sequence.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ynotebook/all.sh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"

for demo in showcase; do
    "$DIR/$demo.sh"
    echo
done
