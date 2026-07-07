#!/bin/bash
# README GIF tour — full ~60s scenario (MSDF zoom, shader plot zoom, ycat).
#
# Prereq: from repo root, start yetty with:
#   ./build-desktop-ytrace-release/yetty --rpc-port=9999 -e bash
#
# Then in another shell:
#   ./demo/scripts/yctl/readme-tour.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_common.sh"
play_asset "$ROOT/demo/assets/yctl/readme-tour.yaml"
