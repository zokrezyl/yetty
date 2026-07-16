#!/bin/bash
# graph.sh — the headline view: the full commit DAG with computed lane columns.
# Watch the feature/release/hotfix merges fan lanes out and back in, and the
# unmerged feature/export branch sit in its own lane.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
ensure_repo

section "ygit graph --all — every branch in one DAG, unmerged lane and all"
ygit -C "$YGIT_DEMO_REPO" graph --all -n 40

section "ygit graph feature/export — the unmerged lane on its own"
ygit -C "$YGIT_DEMO_REPO" graph feature/export -n 40
