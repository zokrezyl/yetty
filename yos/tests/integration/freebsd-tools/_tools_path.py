"""Locate yos host binary + freebsd-tools libexec dir.

Discovery is read-only — never triggers a nix build (that could take
minutes and would blow past meson's 30s timeout). If the artifact
isn't already realized somewhere we know about, return None and let
the caller SKIP.

Lookup order for `result*/`:
  1. `result*/bin/yos` + `result*/libexec/<tool>` — if both present
     (the umbrella `nix build .#all` produces this layout).
  2. Fall back to two separate result symlinks: yos from `nix build
     .#yos`, libexec from `nix build .#freebsd-tools`.

YOS_BIN / YOS_TOOLS_DIR env overrides are respected first.
"""
import os
import glob


def find_yos_and_tools(repo: str):
    yos = os.environ.get("YOS_BIN")
    tools = os.environ.get("YOS_TOOLS_DIR")
    if yos and tools and os.path.exists(yos) and os.path.isdir(tools):
        return yos, tools

    for sym in sorted(glob.glob(os.path.join(repo, "result*"))):
        cand_yos = os.path.join(sym, "bin", "yos")
        cand_libexec = os.path.join(sym, "libexec")
        if os.path.exists(cand_yos) and os.path.isdir(cand_libexec):
            return cand_yos, cand_libexec

    cand_yos = None
    cand_libexec = None
    for sym in sorted(glob.glob(os.path.join(repo, "result*"))):
        if not cand_yos:
            p = os.path.join(sym, "bin", "yos")
            if os.path.exists(p):
                cand_yos = p
        if not cand_libexec:
            p = os.path.join(sym, "libexec")
            if os.path.isdir(p) and os.path.exists(os.path.join(p, "wc")):
                cand_libexec = p
    if cand_yos and cand_libexec:
        return cand_yos, cand_libexec
    return None, None
