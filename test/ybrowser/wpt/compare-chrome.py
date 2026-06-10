#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""
Diff ybrowser's layout against Chrome's, element-for-element by DOM path.

    compare-chrome.py <url> [width]

Runs `ybrowser --dump-geo` and `chrome-oracle.py`, matches boxes by their
identical nth-of-type DOM path, and reports the biggest geometry mismatches —
the layout bugs to fix, ranked so the most structural / top-of-page ones come
first. Env: YBROWSER=<path>, CHROME_GEO_CACHE=1 reuses tmp/chrome-geo.tsv.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
YBROWSER = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")
URL = sys.argv[1] if len(sys.argv) > 1 else "https://news.google.com/"
WIDTH = sys.argv[2] if len(sys.argv) > 2 else "1200"
TOL = 8.0  # px; below this, x/y/w/h count as a match


def parse(text):
    boxes = {}
    for line in text.splitlines():
        c = line.split("\t")
        if len(c) != 5:
            continue
        try:
            boxes[c[0]] = (float(c[1]), float(c[2]), float(c[3]), float(c[4]))
        except ValueError:
            pass
    return boxes


def run_ybrowser():
    r = subprocess.run([YBROWSER, "--dump-geo", "-w", WIDTH, URL],
                       capture_output=True, text=True, timeout=60, cwd=ROOT,
                       env={**os.environ, "DISPLAY": os.environ.get("DISPLAY", ":1.0")})
    return parse(r.stdout)


def run_chrome():
    cache = os.path.join(ROOT, "tmp", "chrome-geo.tsv")
    if os.environ.get("CHROME_GEO_CACHE") == "1" and os.path.exists(cache):
        return parse(open(cache).read())
    r = subprocess.run(["uv", "run", "--script", os.path.join(HERE, "chrome-oracle.py"), URL, WIDTH],
                       capture_output=True, text=True, timeout=120, cwd=ROOT)
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    open(cache, "w").write(r.stdout)
    return parse(r.stdout)


def main():
    yb = run_ybrowser()
    ch = run_chrome()
    shared = [p for p in ch if p in yb]
    only_ch = [p for p in ch if p not in yb]
    print(f"chrome elements: {len(ch)}   ybrowser boxes: {len(yb)}   matched: {len(shared)}")
    print(f"in chrome but MISSING in ybrowser: {len(only_ch)}")

    diffs = []
    for p in shared:
        cx, cy, cw, chh = ch[p]
        yx, yy, yw, yh = yb[p]
        dx, dy, dw, dh = abs(cx - yx), abs(cy - yy), abs(cw - yw), abs(chh - yh)
        worst = max(dx, dy, dw, dh)
        if worst > TOL:
            # rank: structural elements (wide/tall in Chrome) near the top hurt most
            sig = worst * (1.0 + min(cw, 1200) / 1200.0) / (1.0 + cy / 2000.0)
            diffs.append((sig, p, (cx, cy, cw, chh), (yx, yy, yw, yh), (dx, dy, dw, dh)))
    diffs.sort(reverse=True)
    print(f"matched with geometry diff > {TOL:g}px: {len(diffs)}\n")
    print(f"{'dx':>6}{'dy':>6}{'dw':>7}{'dh':>7}   chrome(x,y,w,h) -> ybrowser   path")
    for _, p, c, y, d in diffs[:35]:
        print(f"{d[0]:6.0f}{d[1]:6.0f}{d[2]:7.0f}{d[3]:7.0f}   "
              f"({c[0]:.0f},{c[1]:.0f},{c[2]:.0f},{c[3]:.0f}) -> "
              f"({y[0]:.0f},{y[1]:.0f},{y[2]:.0f},{y[3]:.0f})   {p[-90:]}")
    # biggest missing (wide) chrome elements
    big_missing = sorted(((ch[p][2] * ch[p][3], p, ch[p]) for p in only_ch), reverse=True)[:10]
    if big_missing:
        print("\nbiggest elements present in Chrome but with NO matching ybrowser box:")
        for _, p, c in big_missing:
            print(f"   ({c[0]:.0f},{c[1]:.0f},{c[2]:.0f},{c[3]:.0f})   {p[-90:]}")


if __name__ == "__main__":
    main()
