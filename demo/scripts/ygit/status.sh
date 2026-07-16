#!/bin/bash
# status.sh — the working-tree overview. The demo repo is left dirty on purpose:
# a staged README edit, an unstaged edit to src/app.c, and an untracked
# NOTES.txt — so the two porcelain columns (index / worktree) each light up.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
ensure_repo

section "ygit status — branch, upstream, and the dirty working tree"
ygit -C "$YGIT_DEMO_REPO" status
