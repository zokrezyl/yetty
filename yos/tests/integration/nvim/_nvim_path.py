"""Shared helpers to locate the nvim wasm + the umbrella `tools/yos.sh`
launcher.

Discovery is read-only — never triggers a nix build (that could take
minutes and would blow past meson's 30s timeout). If the artifacts
aren't already realized somewhere we know about, return None and let
the caller SKIP.

The umbrella package (.#all) ships nvim's runtime tree at
$out/share/nvim/runtime — the wasm guest references those .vim/.lua
files at the prefix baked in at compile time. Without them every
init.lua errors with "attempt to index a nil value", and the test pty
captures gibberish. So tests prefer the all-package's nvim over the
plain wasm-pkgs/ output: only the umbrella has the runtime alongside
the binary.

Lookup order:
  1. YOS_ALL_PATH env var → use $YOS_ALL_PATH/{bin/yos,libexec/nvim}
  2. result*/libexec/nvim — symlinks left by a manual `nix build .#all`
  3. `nix path-info .#all` (read-only)
  4. Fall back to `build-linux/wasm-pkgs/nvim-0.10.4/out/bin/nvim.wasm`
     paired with `build-linux/src/yos/yos`. This was the pre-umbrella
     layout — works for nvim --version but not for plugins/init.lua
     since runtime files aren't shipped here.

Returns a dict with keys:
  yos      — path to the host yos binary
  nvim     — path to the nvim wasm module
  has_runtime — True if the runtime tree (share/nvim/runtime) exists
                 at this layout (i.e. `.#all` umbrella).
"""
import os
import subprocess
import glob


def _check_all(prefix: str) -> dict | None:
    yos = os.path.join(prefix, "bin", "yos")
    nvim = os.path.join(prefix, "libexec", "nvim")
    runtime = os.path.join(prefix, "share", "nvim", "runtime", "syntax",
                           "syntax.vim")
    if os.path.exists(yos) and os.path.exists(nvim):
        return {"yos": yos, "nvim": nvim,
                "has_runtime": os.path.exists(runtime)}
    return None


def find_nvim(repo: str) -> dict | None:
    p = os.environ.get("YOS_ALL_PATH")
    if p:
        d = _check_all(p)
        if d: return d

    for sym in sorted(glob.glob(os.path.join(repo, "result*"))):
        d = _check_all(sym)
        if d: return d

    try:
        out = subprocess.run(
            ["nix", "path-info", ".#all"],
            cwd=repo, capture_output=True, text=True, timeout=10,
        )
        if out.returncode == 0:
            d = _check_all(out.stdout.strip())
            if d: return d
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass

    # Pre-umbrella layout — no runtime tree.
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    nvim_legacy = os.path.join(repo, "build-linux", "wasm-pkgs",
                               "nvim-0.10.4", "out", "bin", "nvim.wasm")
    if os.path.exists(yos) and os.path.exists(nvim_legacy):
        return {"yos": yos, "nvim": nvim_legacy, "has_runtime": False}

    return None
