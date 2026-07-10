#!/usr/bin/env bash
# Copy the nix-built tmux wasm into ./tools/tmux.wasm so tmux.html can
# fetch it. The *.wasm files are gitignored (built artifacts), so this
# refreshes the local copy after `nix build .#tmux`.
set -euo pipefail
cd "$(dirname "$0")"

REPO_ROOT=$(cd ../../../.. && pwd)
echo "building .#tmux …"
( cd "$REPO_ROOT" && nix build .#tmux -o "$REPO_ROOT/result-tmux" )
cp "$REPO_ROOT/result-tmux/libexec/tmux" tools/tmux.wasm
echo "copied → tools/tmux.wasm ($(wc -c <tools/tmux.wasm) bytes)"
echo "now: ./serve.sh   then open http://127.0.0.1:8099/tmux.html"
