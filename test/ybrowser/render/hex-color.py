#!/usr/bin/env python3
"""CSS Color L4 hex forms: #RGB, #RGBA, #RRGGBB, #RRGGBBAA.

libcss 0.9 only parsed the 3- and 6-digit forms, DROPPING #RGBA / #RRGGBBAA
as invalid. Modern design systems use them constantly — a `#0000` transparent
override or an 8-digit translucent token (GitHub's `--header-bgColor:
#151b23f2`). A dropped declaration lets an earlier opaque rule win, which
rendered GitHub's dark header as white bars. The parser now accepts all four
forms with alpha. Paint-trace test: the emitted background fill is ABGR
(0xAABBGGRR in the trace's bg= field).

    run: test/ybrowser/render/hex-color.py
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")


def bg_of(css, width=800):
    html = ("<!doctype html><meta charset=utf-8><style>body{margin:0}"
            ".probe{width:80px;height:20px;" + css + "}</style>"
            "<body><div class=probe>x</div></body>")
    env = dict(os.environ); env["YTRACE_DEFAULT_ON"] = "yes"; env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", str(width), "-"], input=html.encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    tr = p.stderr.decode("utf-8", "replace")
    m = re.search(r"wh=80x20 bg=([0-9a-f]{8})", tr)
    return m.group(1) if m else None


# (css, expected trace bg = AABBGGRR). Trace fill is ABGR little-endian-ish:
# for #RRGGBBAA the trace shows GG BB then ... verified empirically as
# 0xAABBGGRR -> we compare against the observed correct values.
# Trace fill field bg= is RRGGBBAA.
CASES = [
    ("background-color:#f80", "ff8800ff"),         # #RGB expands each nibble
    ("background-color:#3a7", "33aa77ff"),         # #RGB
    ("background-color:#ff8800aa", "ff8800aa"),    # #RRGGBBAA -> alpha aa honored
    ("background-color:#0000", "00000000"),        # #RGBA fully transparent
    ("background-color:#00000000", "00000000"),    # #RRGGBBAA transparent
    ("background-color:#123456ff", "123456ff"),    # opaque 8-digit
]


def main():
    fails = 0
    for css, want in CASES:
        got = bg_of(css)
        ok = got == want
        print(f"{'PASS' if ok else 'FAIL'}  {css:34s} bg={got} (want {want})")
        if not ok:
            fails += 1
    # The cascade consequence: a later `#0000` override must beat an earlier
    # opaque rule (it was dropped as invalid before → opaque wrongly won).
    later = bg_of("background-color:#ffffff", 1200)  # sanity: opaque white
    over = ("<!doctype html><meta charset=utf-8><style>body{margin:0}"
            ".c{width:80px;height:20px}"
            "@media(min-width:1012px){.c{background-color:#ffffff}}"
            "@media(min-width:1012px){.c{background-color:#0000}}</style>"
            "<body><div class=c>x</div></body>")
    env = dict(os.environ); env["YTRACE_DEFAULT_ON"] = "yes"; env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", "1200", "-"], input=over.encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    m = re.search(r"wh=80x20 bg=([0-9a-f]{8})", p.stderr.decode("utf-8", "replace"))
    cascade_ok = m and m.group(1) == "00000000"
    print(f"{'PASS' if cascade_ok else 'FAIL'}  later #0000 override beats earlier opaque -> {m.group(1) if m else None}")
    if not cascade_ok:
        fails += 1
    print(f"\n=== {len(CASES)+1-fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
