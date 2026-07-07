#!/bin/bash
# Exercises the modifier path: ctrl+u to clear the line, ctrl+a / ctrl+e to
# navigate, ctrl+c to interrupt. Useful to verify that modifier dispatch
# from the YAML script reaches the terminal correctly.
#
# Prereq: yetty running with --rpc-port=9999 (see hello.sh header).

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_common.sh"
play_asset "$ROOT/demo/assets/yctl/ctrl-keys.yaml"
