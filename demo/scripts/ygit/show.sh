#!/bin/bash
# show.sh — inspect single commits by any revision spec. A tag resolves to its
# commit; a merge commit's file changes are its diff against the first parent.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
ensure_repo

section "ygit show v1.0.0 — the release merge (resolved from a tag)"
ygit -C "$YGIT_DEMO_REPO" show v1.0.0

section "ygit show v1.0.1 — the hotfix merge"
ygit -C "$YGIT_DEMO_REPO" show v1.0.1
