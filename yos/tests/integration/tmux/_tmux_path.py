"""Shared helper to locate the wasm-built tmux binary.

Read-only discovery — never triggers a nix build. Lookup order:
  1. YOS_TMUX_WASM env var (absolute path to the wasm module)
  2. result*/libexec/tmux — symlinks left by `nix build .#tmux` / `.#all`
  3. `nix path-info .#tmux` / `.#all` — used only if the path already
     exists (no realization). Cheap eval.
"""
import os
import subprocess
import glob


def find_tmux_wasm(repo: str) -> str | None:
    override = os.environ.get("YOS_TMUX_WASM")
    if override and os.path.exists(override):
        return override

    candidates = []
    for sym in sorted(glob.glob(os.path.join(repo, "result*"))):
        candidates.append(os.path.join(sym, "libexec", "tmux"))

    for attr in (".#tmux", ".#all"):
        try:
            out = subprocess.run(
                ["nix", "path-info", attr],
                cwd=repo, capture_output=True, text=True, timeout=10,
            )
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        if out.returncode == 0 and out.stdout.strip():
            root = out.stdout.strip()
            candidates.append(os.path.join(root, "libexec", "tmux"))

    for c in candidates:
        if os.path.exists(c):
            return c
    return None
