#!/usr/bin/env python3
"""Render-sanity smoke test — real sites must lay out WITHOUT COLLAPSING.

Runs ybrowser fully HEADLESS (no DISPLAY) with --dump-boxes and asserts the
post-layout box count stays above a per-site floor. This is the standing guard
against the "content column collapsed to ~min-content" class of regression
(the one where Wikipedia's <main> resolved to ~15px and the page became a
350,000px-tall ribbon of one-word lines). Geometry only — no GPU, no pixels,
no display.

    run.py            # all sites
    run.py wikipedia  # one or more by name

Env:
    YBROWSER             path to the ybrowser binary
    RENDER_TIMEOUT       per-site hard wall-clock cap, seconds (default 8)
    YLEXBOR_BOOT_BUDGET_MS  SPA boot pump budget passed through (default 500)

Exit status is non-zero if any site collapses below its floor OR exceeds the
timeout (a timeout is a failure — a real render must not hang headless).
"""
import os
import subprocess
import sys

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")
TIMEOUT = float(os.environ.get("RENDER_TIMEOUT", "8"))

# (name, url, min_boxes). Floors are set well below the observed healthy counts
# so normal page-to-page variance never trips them, but a structural collapse
# (content column → min-content, body dropped) crashes straight through.
SITES = [
    ("wikipedia", "https://en.wikipedia.org/wiki/Photography", 1500),
    ("cnn",       "https://www.cnn.com/",                        800),
    ("gnews",     "https://news.google.com/",                    200),
    ("github",    "https://github.com/",                          40),
]


def count_boxes(url):
    """Return the laid-out box count, or -1 on timeout/hang."""
    env = dict(os.environ)
    env.pop("DISPLAY", None)  # force headless
    env.setdefault("YLEXBOR_BOOT_BUDGET_MS", "500")
    try:
        proc = subprocess.run(
            [YB, "--once", "--dump-boxes", "-w", "1024", url],
            capture_output=True, env=env, timeout=TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        return -1
    n = 0
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        if line and not line.startswith("#"):
            n += 1
    return n


def main():
    which = sys.argv[1:]
    sites = [s for s in SITES if not which or s[0] in which]
    if not sites:
        print(f"no matching sites in {which}", file=sys.stderr)
        return 2
    failures = 0
    for name, url, floor in sites:
        n = count_boxes(url)
        if n < 0:
            print(f"FAIL  {name:10s} TIMEOUT (> {TIMEOUT:.0f}s headless)")
            failures += 1
        elif n < floor:
            print(f"FAIL  {name:10s} {n} boxes (floor {floor} — collapsed?)")
            failures += 1
        else:
            print(f"PASS  {name:10s} {n} boxes (floor {floor})")
    total = len(sites)
    print(f"\n=== {total - failures} passed, {failures} failed ===")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
