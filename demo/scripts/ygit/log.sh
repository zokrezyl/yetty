#!/bin/bash
# log.sh — the commit list with ref decoration, authors, and dates. Note how
# tags (v0.1.0, v1.0.0, v1.0.1) and branch heads annotate the commits they
# point at, and how the two authors alternate.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
ensure_repo

section "ygit log — recent history on main"
ygit -C "$YGIT_DEMO_REPO" log -n 12

section "ygit log v1.0.0 — history up to the release tag"
ygit -C "$YGIT_DEMO_REPO" log v1.0.0 -n 8
