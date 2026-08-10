#!/usr/bin/env python3
"""@layer (CSS Cascade Layers) must not break source-order cascade.

libcss 0.9 has no @layer support: it parses the rules inside `@layer name{}`
but mis-orders them, so equal-specificity rules stop honoring source order —
an earlier rule wrongly beats a later one. Modern design systems wrap their
whole stylesheet in a layer (GitHub ships all of primer-react-brand inside
`@layer primer-brand{...}`), which made the primary signup button lose its
green background to an earlier `.Button{background:0 0}` reset. Box-build now
flattens @layer wrappers (hoisting inner rules to top level) before libcss.

    run: test/ybrowser/render/cascade-layers.py
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")


def bg(css):
    html = ("<!doctype html><meta charset=utf-8><style>body{margin:0}" + css + "</style>"
            "<body><button class='base primary' "
            "style='width:120px;height:30px;display:inline-block'>x</button></body>")
    env = dict(os.environ); env["YTRACE_DEFAULT_ON"] = "yes"; env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", "800", "-"], input=html.encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    m = re.search(r"wh=120x30 bg=([0-9a-f]{8})", p.stderr.decode("utf-8", "replace"))
    return m.group(1) if m else None


def main():
    fails = 0
    cases = [
        # later .primary must beat earlier .base (equal specificity), inside a layer
        ("@layer l{.base{background:0 0}.primary{background-color:#00ff00}}", "00ff00ff",
         "single named layer preserves source order"),
        # nested layers
        ("@layer a{@layer b{.base{background:0 0}.primary{background-color:#00ff00}}}", "00ff00ff",
         "nested layers preserve source order"),
        # anonymous layer
        ("@layer{.base{background:0 0}.primary{background-color:#00ff00}}", "00ff00ff",
         "anonymous layer preserves source order"),
        # layer statement (no block) then rules
        ("@layer a,b;.base{background:0 0}.primary{background-color:#00ff00}", "00ff00ff",
         "layer statement is dropped, rules cascade"),
        # sanity: no layer
        (".base{background:0 0}.primary{background-color:#00ff00}", "00ff00ff",
         "no layer (control)"),
    ]
    for css, want, label in cases:
        got = bg(css)
        ok = got == want
        print(f"{'PASS' if ok else 'FAIL'}  {label} -> bg={got}")
        if not ok:
            fails += 1
    print(f"\n=== {len(cases) - fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
