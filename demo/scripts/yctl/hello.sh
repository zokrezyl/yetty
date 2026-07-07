#!/bin/bash
# Smoke test: types one line into a running yetty over RPC.
#
# Prereq: start yetty with --rpc-port=9999, e.g.
#   ./build-desktop-ytrace-release/yetty --rpc-port=9999 -e bash
#
# Then in another shell:
#   ./demo/scripts/yctl/hello.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_common.sh"
play_asset "$ROOT/demo/assets/yctl/hello.yaml"
