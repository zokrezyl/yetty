#!/usr/bin/env python3
"""Iframe rendering regression test — headless, no GPU, no network.

<iframe> is a nested browsing context: the box builder gives it a replaced box,
the post-layout pass renders its document in a child engine, and paint
composites that child's drawables into the iframe box. This asserts the child's
content actually reaches the paint stream — the geometry harness (WPT/anchors)
cannot see it because the child's boxes live in a separate engine.

Uses `srcdoc` so the child document is inline (no network fetch).

Env:
    YBROWSER   path to the ybrowser binary
Exit 0 pass, 1 fail, 77 skip (binary missing).
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER",
                    "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")


def painted_text(html, width=1000, height=600):
    """Return the concatenation of every painted text run, in emission order."""
    env = dict(os.environ)
    env["YTRACE_DEFAULT_ON"] = "yes"
    env.pop("DISPLAY", None)
    proc = subprocess.run(
        [YB, "--once", "-w", str(width), "-H", str(height), "-"],
        input=html.encode("utf-8"), stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, timeout=60, env=env)
    trace = proc.stderr.decode("utf-8", errors="replace")
    runs = re.findall(r'paint text .*?"([^"]*)"', trace)
    return " ".join(runs)


CASES = []


def case(name, html, needles):
    CASES.append((name, html, needles))


# Child text (from srcdoc) must appear in the parent's paint stream — proof the
# nested document rendered and composited.
case("srcdoc child content composites",
     "<!doctype html><meta charset=utf-8><body style='margin:0'>"
     "<h1>PARENT_HEADING</h1>"
     "<iframe width='600' height='200' "
     "srcdoc=\"<body><h2>CHILD_HEADING</h2><p>CHILD_BODY_TEXT</p></body>\">"
     "</iframe></body>",
     ["PARENT_HEADING", "CHILD_HEADING", "CHILD_BODY_TEXT"])

# A nested iframe (child contains its own iframe) renders one level deeper.
case("nested iframe composites one level deeper",
     "<!doctype html><meta charset=utf-8><body style='margin:0'>"
     "<iframe width='600' height='300' "
     "srcdoc=\"<body>OUTER_FRAME<iframe width=400 height=120 "
     "srcdoc='&lt;body&gt;INNER_FRAME&lt;/body&gt;'></iframe></body>\">"
     "</iframe></body>",
     ["OUTER_FRAME", "INNER_FRAME"])


def main():
    if not os.path.exists(YB):
        print(f"SKIP: ybrowser binary not found at {YB}")
        return 77
    failed = 0
    for name, html, needles in CASES:
        text = painted_text(html)
        missing = [n for n in needles if n not in text]
        ok = not missing
        print(f"{'PASS' if ok else 'FAIL'}  {name}"
              f"{'' if ok else '  missing=' + ','.join(missing)}")
        if not ok:
            failed += 1
    print(f"\n=== {len(CASES) - failed}/{len(CASES)} iframe cases passed ===")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
