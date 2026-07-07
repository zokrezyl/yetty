#!/bin/bash
# Drives a short, scripted shell session against a running yetty. Pair with
# yetty --record to capture a reproducible MP4.
#
# Run this from a NORMAL terminal, never from inside the yetty being driven
# (the busy shell there could not read the injected keystrokes — they would
# all execute in one batch when this script exits).
#
# One-shot recording recipe:
#   ./build-desktop-ytrace-release/yetty \
#       --record /tmp/yctl-demo.mp4 --rpc-port=9999 -e bash &
#   ./demo/scripts/yctl/shell-session.sh
#   # close the yetty window when the script finishes
#
# The shell-session script ends with `exit`, so bash will quit and yetty
# closes its window soon after — at which point the MP4 is finalized.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_common.sh"
play_asset "$ROOT/demo/assets/yctl/shell-session.yaml"
