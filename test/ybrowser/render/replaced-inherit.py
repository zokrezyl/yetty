#!/usr/bin/env python3
"""Replaced elements (<svg>, <img>) inherit opacity/visibility from ancestors.

Inline SVG and <img> boxes are built on a separate path that copied only fg
and font-size from the ancestor style state — NOT the folded group opacity
or the visibility flag. So an icon inside an opacity:0 / visibility:hidden
subtree still painted. GitHub's collapsed nav dropdown panels (opacity:0;
visibility:hidden) then scattered their octicons across the hero. The box
now inherits both, so a hidden/transparent subtree suppresses its icons.

Paint-trace test: a replaced element paints a `paint image` command; count
how many appear.

    run: test/ybrowser/render/replaced-inherit.py
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")
SVG = "<svg width=16 height=16 viewBox='0 0 16 16'><rect width=16 height=16/></svg>"


def image_paint_count(inner_style):
    html = ("<!doctype html><meta charset=utf-8><body style=margin:0>"
            "<div style='" + inner_style + "'>" + SVG + "</div></body>")
    env = dict(os.environ); env["YTRACE_DEFAULT_ON"] = "yes"; env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", "800", "-"], input=html.encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    tr = p.stderr.decode("utf-8", "replace")
    # A visible inline SVG emits an svg_inline scene resolve; a
    # hidden box is skipped before paint dispatch (no such line).
    return len(re.findall(r"svg_inline", tr))


def main():
    fails = 0
    cases = [
        ("visibility:hidden", 0, "svg in visibility:hidden subtree"),
        ("opacity:0", 0, "svg in opacity:0 subtree"),
        ("opacity:0.005", 0, "svg in effectively-transparent subtree"),
        ("", 1, "svg in a normal subtree still paints"),
        ("visibility:visible", 1, "svg in explicitly-visible subtree paints"),
    ]
    for style, want, label in cases:
        n = image_paint_count(style)
        ok = (n == 0) if want == 0 else (n >= 1)
        print(f"{'PASS' if ok else 'FAIL'}  {label} -> {n} svg paint(s)")
        if not ok:
            fails += 1
    print(f"\n=== {len(cases) - fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
