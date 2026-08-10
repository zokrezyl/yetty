#!/usr/bin/env python3
"""@layer cascade PRIORITY — the part a flatten-to-source-order hack cannot do.

libcss now models cascade layers natively (a per-rule layer order compared
before specificity in the cascade). This test pins the four behaviours that
distinguish real layer support from merely hoisting @layer rules to top level:

  1. A later layer beats an earlier one EVEN AT LOWER SPECIFICITY
     (the Tailwind/Bootstrap "utilities override components" pattern).
  2. Unlayered author rules beat ANY layered rule, regardless of specificity.
  3. Layer declaration order (via `@layer a, b;`) wins over source order.
  4. `!important` reverses layer order: the earlier layer wins.

Plus the two nesting shapes frameworks actually ship:
  5. @media nested inside @layer.
  6. @layer nested inside @media.

    run: test/ybrowser/render/cascade-layer-priority.py
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")

RED = "ff0000ff"
GREEN = "00ff00ff"


def bg(css, cls):
    html = ("<!doctype html><meta charset=utf-8><style>body{margin:0}" + css + "</style>"
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
    # (css, element-class, expected-bg, label)
    cases = [
        ("@layer components{.a.b{background-color:#ff0000}}"
         "@layer utilities{.a{background-color:#00ff00}}",
         "a b", GREEN,
         "later layer beats earlier at LOWER specificity (utility override)"),

        ("@layer c{.a.b{background-color:#ff0000}}"
         ".a{background-color:#00ff00}",
         "a b", GREEN,
         "unlayered beats layered regardless of specificity"),

        ("@layer first,second;"
         "@layer second{.a{background-color:#ff0000}}"
         "@layer first{.a{background-color:#00ff00}}",
         "a", RED,
         "layer declaration order wins over source order"),

        ("@layer first,second;"
         "@layer first{.a{background-color:#ff0000 !important}}"
         "@layer second{.a{background-color:#00ff00 !important}}",
         "a", RED,
         "!important reverses layer order (earlier layer wins)"),

        ("@layer u{@media (min-width:1px){.a{background-color:#00ff00}}}",
         "a", GREEN,
         "@media nested inside @layer applies"),

        ("@media (min-width:1px){@layer u{.a{background-color:#00ff00}}}",
         "a", GREEN,
         "@layer nested inside @media applies"),
    ]
    for css, cls, want, label in cases:
        got = bg(css, cls)
        ok = got == want
        print(f"{'PASS' if ok else 'FAIL'}  {label} -> bg={got} (want {want})")
        if not ok:
            fails += 1
    print(f"\n=== {len(cases) - fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
