#!/bin/bash
# Drives a short, scripted shell session against a running yetty. Pair with
# yetty --record to capture a reproducible MP4.
#
# One-shot recording recipe:
#   ./build-desktop-ytrace-release/yetty \
#       --record /tmp/yctl-demo.mp4 --rpc-port=9999 -e bash &
#   sleep 1
#   ./demo/scripts/yctl-scripts/shell-session.sh
#   # close the yetty window when the script finishes
#
# The shell-session script ends with `exit`, so bash will quit and yetty
# closes its window soon after — at which point the MP4 is finalized.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_common.sh"
play_asset "$ROOT/demo/assets/yctl-scripts/shell-session.yaml"
