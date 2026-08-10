#!/usr/bin/env python3
"""Visually-hidden accessibility idiom must not PAINT.

`.sr-only` / `.visually-hidden` collapse an element to a 1x1 box that clips
its overflow (`width:1px;height:1px;overflow:hidden` + `clip`/`clip-path`),
keeping the text for screen readers while hiding it visually. ybrowser has no
per-pixel scissor, so without special handling the clipped-away text painted
at full size — GitHub's header showed stray "Skip to content" / "Navigation
Menu" labels and white bars over the nav. Box-build now marks such a box
`visibility:hidden` (paint suppressed, tiny layout box kept).

This is a PAINT test: render headless with tracing and assert the hidden
label never reaches a paint-text command while a normal sibling does.

    run: test/ybrowser/render/sr-only.py
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")


def painted_text(html, width=800, height=400):
    """UTF-8 text of every `paint text ... "<text>"` trace line, draw order."""
    env = dict(os.environ)
    env["YTRACE_DEFAULT_ON"] = "yes"
    env.pop("DISPLAY", None)
    proc = subprocess.run(
        [YB, "--once", "-w", str(width), "-H", str(height), "-"],
        input=html.encode("utf-8"), stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, timeout=60, env=env)
    trace = proc.stderr.decode("utf-8", errors="replace")
    return [m.group(1) for m in re.finditer(r'paint text .*?"([^"]*)"', trace)]


# Each case: the CSS applied to the hidden element. The visible sentinel must
# always paint; the hidden sentinel must never paint.
HIDE_RULES = {
    "width/height 1px + overflow:hidden":
        "position:absolute;width:1px;height:1px;overflow:hidden",
    "sr-only classic (+clip rect)":
        "position:absolute;width:1px;height:1px;overflow:hidden;"
        "clip:rect(0,0,0,0);white-space:nowrap;border:0;margin:-1px;padding:0",
    "primer visually-hidden (+clip-path)":
        "width:1px;height:1px;clip-path:rect(0 0 0 0);overflow:hidden;"
        "border:0;padding:0;position:absolute",
}

TEMPLATE = (
    "<!doctype html><meta charset=utf-8><style>body{{margin:0}}"
    ".hide{{{rule}}}</style>"
    "<body><div>SENTINELVISIBLE</div>"
    "<h2 class=hide>SENTINELHIDDEN</h2>"
    "<a class=hide href=#>SENTINELHIDDEN</a></body>"
)


def main():
    failures = 0
    for label, rule in HIDE_RULES.items():
        painted = painted_text(TEMPLATE.format(rule=rule))
        blob = " ".join(painted)
        visible_ok = "SENTINELVISIBLE" in blob
        hidden_leaked = "SENTINELHIDDEN" in blob
        ok = visible_ok and not hidden_leaked
        status = "PASS" if ok else "FAIL"
        detail = ""
        if not visible_ok:
            detail += " visible-sentinel-not-painted"
        if hidden_leaked:
            detail += " hidden-text-LEAKED-to-paint"
        print(f"{status}  {label}{detail}")
        if not ok:
            failures += 1

    # A genuine 1px hairline that does NOT clip overflow must still paint its
    # content — the collapse only fires when overflow is clipped on both tiny
    # axes, so a decorative rule is unaffected.
    hairline = TEMPLATE.format(rule="width:1px;height:1px;overflow:visible")
    painted = " ".join(painted_text(hairline))
    if "SENTINELHIDDEN" not in painted:
        print("FAIL  non-clipping 1px box wrongly suppressed its content")
        failures += 1
    else:
        print("PASS  non-clipping 1px box still paints (no over-hiding)")

    total = len(HIDE_RULES) + 1
    print(f"\n=== {total - failures} passed, {failures} failed ===")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
