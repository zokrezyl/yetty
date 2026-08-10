#!/usr/bin/env python3
"""ES modules — <script type="module">, import/export, import.meta, import maps.

GitHub (and most modern sites) ship their JS as ES modules; evaluating those as
classic scripts throws `unsupported keyword: export` and the whole app dies.
This pins the module machinery end-to-end via file:// modules, observing the
result as a background colour a module sets on a <div> after importing it.

  1. import { export } from "./relative.mjs" resolves and runs.
  2. import.meta.url is the module's own URL.
  3. A bare specifier resolves through <script type="importmap">.
  4. A transitive import chain (a -> b -> c) links and evaluates.

    run: test/ybrowser/render/es-modules.py
"""
import os
import re
import subprocess
import sys
import tempfile

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")

GREEN = "00ff00ff"
RED = "ff0000ff"


def bg(html_path):
    env = dict(os.environ)
    env["YTRACE_DEFAULT_ON"] = "yes"
    env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", "800", html_path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    m = re.search(r"wh=120x30 bg=([0-9a-f]{8})", p.stderr.decode("utf-8", "replace"))
    return m.group(1) if m else None


PAGE = ("<!doctype html><meta charset=utf-8><style>body{{margin:0}}</style>"
        "<body><div class='x' style='width:120px;height:30px;display:inline-block'>x</div>"
        "{head}"
        "<script type=\"module\" src=\"file://{main}\"></script></body>")


def write(d, name, text):
    path = os.path.join(d, name)
    with open(path, "w") as f:
        f.write(text)
    return path


def main():
    fails = 0
    total = 0
    with tempfile.TemporaryDirectory() as d:
        cases = []

        # 1. relative import + export + DOM mutation
        write(d, "dep.mjs", 'export const color = "#00ff00";\n')
        m1 = write(d, "main1.mjs",
                   'import { color } from "./dep.mjs";\n'
                   'document.querySelector(".x").style.background = color;\n')
        p1 = write(d, "p1.html", PAGE.format(head="", main=m1))
        cases.append((p1, GREEN, "relative import/export mutates the DOM"))

        # 2. import.meta.url is the module's own file URL
        m2 = write(d, "main2.mjs",
                   'const ok = import.meta.url.endsWith("/main2.mjs");\n'
                   'document.querySelector(".x").style.background = ok ? "#00ff00" : "#ff0000";\n')
        p2 = write(d, "p2.html", PAGE.format(head="", main=m2))
        cases.append((p2, GREEN, "import.meta.url is the module URL"))

        # 3. bare specifier resolved through an import map
        m3 = write(d, "main3.mjs",
                   'import { color } from "palette";\n'
                   'document.querySelector(".x").style.background = color;\n')
        head3 = ('<script type="importmap">{"imports":{"palette":"file://' + d +
                 '/dep.mjs"}}</script>')
        p3 = write(d, "p3.html", PAGE.format(head=head3, main=m3))
        cases.append((p3, GREEN, "bare specifier resolves through importmap"))

        # 4. transitive import chain a -> b -> c
        write(d, "c.mjs", 'export const v = "#00ff00";\n')
        write(d, "b.mjs", 'import { v } from "./c.mjs";\nexport const w = v;\n')
        m4 = write(d, "main4.mjs",
                   'import { w } from "./b.mjs";\n'
                   'document.querySelector(".x").style.background = w;\n')
        p4 = write(d, "p4.html", PAGE.format(head="", main=m4))
        cases.append((p4, GREEN, "transitive import chain (a->b->c) links"))

        for path, want, label in cases:
            total += 1
            got = bg(path)
            ok = got == want
            print(f"{'PASS' if ok else 'FAIL'}  {label} -> bg={got} (want {want})")
            if not ok:
                fails += 1

    print(f"\n=== {total - fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
