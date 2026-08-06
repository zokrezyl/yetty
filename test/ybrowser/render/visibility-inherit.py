#!/usr/bin/env python3
"""visibility:hidden inherits to descendants; siblings are unaffected.

ybrowser selects each element's computed style independently (no parent
style threaded into libcss), so the INHERITED `visibility` never reached
descendants through the cascade — a child of a visibility:hidden container
computed `visible` and painted. GitHub's collapsed nav dropdown panels
(visibility:hidden) then painted their octicons/labels. Box-build now
propagates the hidden state through its own walk (sticky: once an ancestor
is hidden the subtree stays hidden), while independent sibling subtrees are
untouched (each copies the parent state by value).

    run: test/ybrowser/render/visibility-inherit.py
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")


def painted(html):
    env = dict(os.environ); env["YTRACE_DEFAULT_ON"] = "yes"; env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", "800", "-"], input=html.encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    tr = p.stderr.decode("utf-8", "replace")
    return " ".join(m.group(1) for m in re.finditer(r'paint text .*?"([^"]*)"', tr))


def main():
    fails = 0

    # 1. A child (deeply nested) of a visibility:hidden container must not paint.
    html = ("<!doctype html><meta charset=utf-8><body style=margin:0>"
            "<div style='visibility:hidden'><span><b>NESTEDHIDDEN</b></span></div>"
            "<div>PLAINVISIBLE</div></body>")
    blob = painted(html)
    if "NESTEDHIDDEN" in blob:
        print("FAIL  descendant of visibility:hidden painted"); fails += 1
    else:
        print("PASS  descendant of visibility:hidden is suppressed")
    if "PLAINVISIBLE" not in blob:
        print("FAIL  following sibling wrongly suppressed (leak)"); fails += 1
    else:
        print("PASS  following sibling still paints (no sibling leak)")

    # 2. A hidden container between two visible siblings must not bleed onto the
    #    second sibling.
    html2 = ("<!doctype html><meta charset=utf-8><body style=margin:0>"
             "<div>BEFOREVIS</div>"
             "<div style='visibility:hidden'><i>MIDHIDDEN</i></div>"
             "<div>AFTERVIS</div></body>")
    blob2 = painted(html2)
    ok2 = ("BEFOREVIS" in blob2) and ("AFTERVIS" in blob2) and ("MIDHIDDEN" not in blob2)
    if ok2:
        print("PASS  hidden middle sibling suppressed, both neighbors paint")
    else:
        print(f"FAIL  sibling isolation: before={'BEFOREVIS' in blob2} "
              f"after={'AFTERVIS' in blob2} hidden-leak={'MIDHIDDEN' in blob2}")
        fails += 1

    print(f"\n=== {3 - fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
