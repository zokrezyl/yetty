#!/bin/bash
# all.sh — rebuild the demo repo, then run every ygit view in sequence.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ygit/all.sh
#   demo/scripts/ygit/all.sh            # or just run it in a plain shell
#
# Knob: DEMO_PAUSE=<seconds> pauses between views (default 1).
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$DIR/common.sh"
PAUSE="${DEMO_PAUSE:-1}"

# Fresh repo every run so the story is reproducible.
rm -rf "$YGIT_DEMO_REPO"
bash "$DIR/build-repo.sh"

# The git-like views first, then the ones only yetty can do (graph lanes, the
# rendered-from-history `view`, and the drawn before/after `diff`).
for scene in status branches log show graph view diff; do
    bash "$DIR/$scene.sh"
    sleep "$PAUSE"
done

section "done — the demo repo is at $YGIT_DEMO_REPO"
