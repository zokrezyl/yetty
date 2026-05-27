#!/bin/bash
# Demonstrates the typing knobs: avg-speed, speed-deviation, avg-typo.
# Watch three lines appear with different cadence and one with typo+backspace.
#
# Prereq: yetty running with --rpc-port=9999 (see hello.sh header).

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_common.sh"
play_asset "$ROOT/demo/assets/yctl-scripts/typing-styles.yaml"
