#!/usr/bin/env python3
"""@layer completeness — hierarchical nesting order and cross-sheet ordering.

These pin the two behaviours a flat per-sheet layer counter cannot get right:

  1. Nested layers are ordered by tree position, not first-appearance. A
     sub-layer `a.b` declared AFTER a later root layer `c` still lives inside
     a's slot, so `c` (a later root) outranks it. A flat counter would give
     a.b the largest number and let it win — wrong.

  2. Cascade layers are ordered document-wide: the same layer name (and the
     relative order of different names) is shared across separate sheets, so a
     rule in a later layer from sheet B beats a higher-specificity rule in an
     earlier layer from sheet A. Per-sheet numbering would tie them and let
     specificity decide — wrong.

    run: test/ybrowser/render/cascade-layer-advanced.py
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")

RED = "ff0000ff"
GREEN = "00ff00ff"
BLUE = "0000ffff"


def bg(head_css, cls):
    """head_css is raw <style>...</style> markup (may be several blocks)."""
    html = ("<!doctype html><meta charset=utf-8>" + head_css +
            "<style>body{margin:0}</style>"
            "<body><div class='" + cls + "' "
            "style='width:120px;height:30px;display:inline-block'>x</div></body>")
    env = dict(os.environ)
    env["YTRACE_DEFAULT_ON"] = "yes"
    env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", "800", "-"], input=html.encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    m = re.search(r"wh=120x30 bg=([0-9a-f]{8})", p.stderr.decode("utf-8", "replace"))
    return m.group(1) if m else None


def main():
    fails = 0
    # (head-markup, element-class, expected, label)
    cases = [
        # Nested: a.b declared last, but c (a later root layer) must still win.
        ("<style>"
         "@layer a{.x{background-color:#ff0000}}"
         "@layer c{.x{background-color:#00ff00}}"
         "@layer a.b{.x{background-color:#0000ff}}"
         "</style>",
         "x", GREEN,
         "later root layer beats a deeper sub-layer declared afterwards"),

        # Nested: within layer a, direct rules outrank a sub-layer's rules.
        ("<style>"
         "@layer a{@layer b{.x{background-color:#ff0000}}"
         ".x{background-color:#00ff00}}"
         "</style>",
         "x", GREEN,
         "rules directly in a layer outrank its sub-layer"),

        # Cross-sheet: second layer (sheet B) beats first layer (sheet A) even
        # though sheet A's rule has higher specificity.
        ("<style>@layer first,second;</style>"
         "<style>@layer first{.x.y{background-color:#ff0000}}</style>"
         "<style>@layer second{.x{background-color:#00ff00}}</style>",
         "x y", GREEN,
         "cross-sheet: later layer beats earlier at higher specificity"),

        # Cross-sheet: the same layer name in two sheets is ONE layer; source
        # order across sheets then decides (sheet B is later -> wins).
        ("<style>@layer base{.x{background-color:#ff0000}}</style>"
         "<style>@layer base{.x{background-color:#00ff00}}</style>",
         "x", GREEN,
         "cross-sheet: same layer name is shared, later sheet wins"),
    ]
    for head, cls, want, label in cases:
        got = bg(head, cls)
        ok = got == want
        print(f"{'PASS' if ok else 'FAIL'}  {label} -> bg={got} (want {want})")
        if not ok:
            fails += 1
    print(f"\n=== {len(cases) - fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
