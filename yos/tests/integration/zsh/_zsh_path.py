"""Shared helper to locate the wasm-built zsh binary.

Discovery is read-only — never triggers a nix build (that could take
minutes and would blow past meson's 30s timeout). If the artifact
isn't already realized somewhere we know about, return None and let
the caller SKIP.

Lookup order:
  1. YOS_ZSH_WASM env var (absolute path to .wasm)
  2. result*/bin/zsh.wasm — symlinks left by a manual `nix build .#zsh`
  3. `nix path-info .#zsh` — uses *only* if the path already exists
     (no realization). Cheap eval: ~100ms.
"""
import os
import subprocess
import glob


def find_zsh_wasm(repo: str) -> str | None:
    p = os.environ.get("YOS_ZSH_WASM")
    if p and os.path.exists(p):
        return p

    # The nix package layout moved: bare wasm modules now live under
    # $out/libexec/<name> (no suffix), with shell-runner wrappers in
    # $out/bin/<name>. Keep the legacy $out/bin/zsh.wasm path as a
    # fallback for old result/ symlinks. Either nix .#zsh or .#all
    # works — .#all has the same libexec/zsh.
    candidates = []
    for sym in sorted(glob.glob(os.path.join(repo, "result*"))):
        candidates.append(os.path.join(sym, "libexec", "zsh"))
        candidates.append(os.path.join(sym, "bin", "zsh.wasm"))

    for attr in (".#zsh", ".#all"):
        try:
            out = subprocess.run(
                ["nix", "path-info", attr],
                cwd=repo, capture_output=True, text=True, timeout=10,
            )
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        if out.returncode == 0 and out.stdout.strip():
            root = out.stdout.strip()
            candidates.append(os.path.join(root, "libexec", "zsh"))
            candidates.append(os.path.join(root, "bin", "zsh.wasm"))

    for c in candidates:
        if os.path.exists(c):
            return c
    return None
