#!/usr/bin/env python3
"""`@import ... layer(name)` places an imported sheet into a cascade layer.

CSS Cascade 5 lets `@import url(...) layer(base)` drop the whole imported sheet
into a named layer. This test proves the imported rules become layered: an
imported HIGH-specificity rule in `layer(base)` must LOSE to a local
LOW-specificity UNLAYERED rule (layers beat specificity), while the same import
WITHOUT layer() keeps the imported rule unlayered so its higher specificity
wins. The only difference between the two documents is the `layer(base)` clause,
so a flip in the result isolates the feature.

Uses a real file:// import (the loader rejects data: URIs).

    run: test/ybrowser/render/cascade-layer-import.py
"""
import os
import re
import subprocess
import sys
import tempfile

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")

RED = "ff0000ff"
GREEN = "00ff00ff"


def bg(html):
    env = dict(os.environ)
    env["YTRACE_DEFAULT_ON"] = "yes"
    env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", "800", "-"], input=html.encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    m = re.search(r"wh=120x30 bg=([0-9a-f]{8})", p.stderr.decode("utf-8", "replace"))
    return m.group(1) if m else None


def doc(import_url, layer_clause, main_css, cls="x y"):
    return (
        "<!doctype html><meta charset=utf-8>"
        "<style>@import url(\"" + import_url + "\")" + layer_clause + ";"
        + main_css + "</style>"
        "<style>body{margin:0}</style>"
        "<body><div class='" + cls + "' "
        "style='width:120px;height:30px;display:inline-block'>x</div></body>"
    )


def main():
    fails = 0
    total = 0
    with tempfile.TemporaryDirectory() as td:
        flat_css = os.path.join(td, "flat.css")
        with open(flat_css, "w") as f:
            f.write(".x.y{background-color:#00ff00}\n")  # high specificity, green
        flat_url = "file://" + flat_css

        # An imported sheet that itself contains a NESTED @layer block. Under
        # `@import layer(x)` this must become x.deep, a sub-layer inside x.
        nested_css = os.path.join(td, "nested.css")
        with open(nested_css, "w") as f:
            f.write("@layer deep { .t{background-color:#ff0000} }\n")
        nested_url = "file://" + nested_css

        cases = [
            (doc(flat_url, " layer(base)", ".x{background-color:#ff0000}"), RED,
             "imported layer(base) rule loses to local unlayered rule (layers beat specificity)"),
            (doc(flat_url, "", ".x{background-color:#ff0000}"), GREEN,
             "control: imported unlayered rule keeps its higher specificity and wins"),

            # Nested @layer inside an @import layer(x) sheet resolves UNDER x.
            # x is declared first (by the @import), then y; so y outranks
            # x.deep and the local green wins. If x.deep leaked to the registry
            # root it would be declared AFTER y and wrongly win (red).
            (doc(nested_url, " layer(x)", "@layer y{.t{background-color:#00ff00}}", cls="t"),
             GREEN,
             "nested @layer inside @import layer(x) resolves under x (x.deep < y)"),
        ]
        for html, want, label in cases:
            total += 1
            got = bg(html)
            ok = got == want
            print(f"{'PASS' if ok else 'FAIL'}  {label} -> bg={got} (want {want})")
            if not ok:
                fails += 1

    print(f"\n=== {total - fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
