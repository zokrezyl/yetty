#!/usr/bin/env python3
"""Two real-site JS gaps: the Intl polyfill and document.currentScript.

Both were found rendering nytimes.com: QuickJS-ng ships no Intl (ICU), so any
bundle touching it dies with `ReferenceError: Intl is not defined` (it killed
nytimes's MAIN bundle); and document.currentScript was hard-coded null, so the
common inline-mount idiom `document.currentScript.previousElementSibling` threw.

Each case runs a script that sets a <div>'s background green only when the API
behaves correctly, so the render trace's bg colour is the pass/fail signal.

    run: test/ybrowser/render/js-intl-currentscript.py
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")
GREEN = "00ff00ff"


def bg(body):
    html = ("<!doctype html><meta charset=utf-8><style>body{margin:0}</style><body>" + body +
            "</body>")
    env = dict(os.environ)
    env["YTRACE_DEFAULT_ON"] = "yes"
    env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", "800", "-"], input=html.encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    m = re.search(r"wh=120x30 bg=([0-9a-f]{8})", p.stderr.decode("utf-8", "replace"))
    return m.group(1) if m else None


DIV = "<div class='x' style='width:120px;height:30px;display:inline-block'>x</div>"

INTL = DIV + """<script>
var ok =
  new Intl.NumberFormat('en-US').format(1234567.5) === "1,234,567.5" &&
  new Intl.NumberFormat('en-US',{style:'currency',currency:'USD'}).format(9.5) === "$9.50" &&
  new Intl.DateTimeFormat('en-US',{year:'numeric',month:'long',day:'numeric'})
      .format(new Date(2026,6,3)) === "July 3, 2026" &&
  new Intl.RelativeTimeFormat('en-US').format(-2,'day') === "2 days ago" &&
  new Intl.ListFormat('en-US').format(['a','b','c']) === "a, b, and c" &&
  new Intl.PluralRules('en-US').select(1) === "one" &&
  new Intl.PluralRules('en-US').select(3) === "other";
document.querySelector('.x').style.background = ok ? "#00ff00" : "#ff0000";
</script>"""

# The inline script sits immediately after the div; currentScript.previousElementSibling
# must resolve to that div (a common SSR mount-anchor idiom).
CURRENT_SCRIPT = DIV + """<script>
var el = document.currentScript && document.currentScript.previousElementSibling;
if (el && el.classList.contains('x')) { el.style.background = "#00ff00"; }
</script>"""


def main():
    cases = [
        (INTL, "Intl.{NumberFormat,DateTimeFormat,RelativeTimeFormat,ListFormat,PluralRules}"),
        (CURRENT_SCRIPT, "document.currentScript.previousElementSibling anchors the mount"),
    ]
    fails = 0
    for body, label in cases:
        got = bg(body)
        ok = got == GREEN
        print(f"{'PASS' if ok else 'FAIL'}  {label} -> bg={got}")
        if not ok:
            fails += 1
    print(f"\n=== {len(cases) - fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
