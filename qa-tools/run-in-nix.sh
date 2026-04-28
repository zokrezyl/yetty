#!/usr/bin/env bash
# Run a qa-tools script (or any command) inside the flake's default devShell.
# Usage: qa-tools/run-in-nix.sh qa-tools/analysis/check-clang-tidy.py [args...]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec nix develop "$REPO_ROOT" --command "$@"
