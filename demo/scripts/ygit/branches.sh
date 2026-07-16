#!/bin/bash
# branches.sh — the local branch overview: the checked-out branch is marked, and
# each branch shows its tip's short hash and subject. feature/export is the one
# that never merged back.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
ensure_repo

section "ygit branches — local branches with tip + subject"
ygit -C "$YGIT_DEMO_REPO" branches
