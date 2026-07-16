#!/bin/bash
# view.sh — the reason to switch. `ygit view <rev>:<path>` pulls a file's blob
# straight from history and RENDERS it inline (inside yetty): an SVG is drawn,
# Markdown is formatted, source is syntax-highlighted. `git show` can only print
# the raw bytes. Run this inside yetty to see the figures:
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ygit/view.sh
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
ensure_repo

section "ygit view v0.1.0:assets/logo.svg — the original logo, drawn from history"
ygit -C "$YGIT_DEMO_REPO" view v0.1.0:assets/logo.svg

section "ygit view HEAD:assets/logo.svg — the redesigned logo at HEAD"
ygit -C "$YGIT_DEMO_REPO" view HEAD:assets/logo.svg

section "ygit view HEAD:docs/guide.md — rendered Markdown (not raw source)"
ygit -C "$YGIT_DEMO_REPO" view HEAD:docs/guide.md

section "ygit view HEAD:src/login.c — syntax-highlighted source from history"
ygit -C "$YGIT_DEMO_REPO" view HEAD:src/login.c
