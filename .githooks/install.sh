#!/usr/bin/env bash
# Activate the tracked hooks for the current clone.
# Run once after cloning; idempotent.
set -e
cd "$(dirname "$0")/.."
chmod +x .githooks/pre-commit .githooks/pre-push
git config core.hooksPath .githooks
echo "core.hooksPath set to .githooks — hooks active."
echo "allowed emails:"
. .githooks/lib-codeowners.sh
codeowners_emails | sed 's/^/  /'
