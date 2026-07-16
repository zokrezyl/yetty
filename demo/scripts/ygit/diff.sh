#!/bin/bash
# diff.sh — the diff git cannot draw. `ygit diff <rev>` renders a commit's diff
# against its first parent:
#   - a renderable asset (SVG / image / PDF) is drawn as a visual BEFORE / AFTER
#     — the old logo above the new one, as figures, not XML;
#   - source is a unified diff with the code syntax-highlighted by its own
#     grammar and the changed lines banded — past git's line-level red/green.
# Run inside yetty to see the asset before/after as figures:
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ygit/diff.sh
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
ensure_repo

# HEAD is the logo-redesign commit, so its diff is a pure asset change: the old
# ring logo and the new diamond logo, drawn side by side straight from history.
section "ygit diff HEAD — the logo redesign, drawn before/after (git would print XML)"
ygit -C "$YGIT_DEMO_REPO" diff HEAD

# The crash hotfix touched a C source file — shown as a syntax-highlighted patch.
HOTFIX="$(git -C "$YGIT_DEMO_REPO" log --all --grep 'guard against null argv' --format='%H' | head -1)"
section "ygit diff <hotfix> — src/app.c as a syntax-highlighted diff"
ygit -C "$YGIT_DEMO_REPO" diff "$HOTFIX"
