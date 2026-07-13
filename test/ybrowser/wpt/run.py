#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""
WPT-style test runner for the ybrowser layout engine.

Web Platform Tests proper are pixel reftests; ybrowser has no headless pixel
pipeline in CI, but it CAN dump the post-layout box tree (`--dump-boxes`), so
these tests assert on box GEOMETRY. Two test shapes, both standard-WPT-shaped:

  1. Self-assert: an element carries
         data-test="<name>" data-expect="<x>,<y>,<w>,<h>"
     Each field is an absolute px value, or `*` to skip it. The box with that
     data-test name must match (±1.5px). Best for basic box-model facts where
     the expected number is obvious.

  2. Reftest (WPT `<link rel="match" href="...-ref.html">`): the test and its
     reference must produce the SAME geometry for every shared data-test name.
     Best for "feature X lays out identically to a simpler construction Y".

Tests live under test/ybrowser/wpt/<NN-category>/<name>.html (refs: <name>-ref.html).
The leading NN orders categories basic -> advanced. Run:

    test/ybrowser/wpt/run.py                 # all
    test/ybrowser/wpt/run.py 01 06           # only categories whose path contains 01/06
"""
import glob
import os
import re
import subprocess
import sys

YBROWSER = os.environ.get(
    "YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser"
)
WIDTH = os.environ.get("WPT_WIDTH", "800")
TOL = 1.5
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

# Fixtures that are known to fail and are deliberately kept OUT of the
# required-green lane, so a triaged regression does not make `make test-wpt`
# ambiguous. Keep this EMPTY unless a failure is actively being tracked — each
# entry silences a real failure, so it should be temporary and reference an
# issue in the comment. Paths are relative to this directory, e.g.
# "05-flexbox/gap.html".
KNOWN_FAILURES = set()

# ctest skip code — see docs/testing.md. Returned when the ybrowser binary is
# not built (e.g. YETTY_ENABLE_FEATURE_YBROWSER=OFF) so the suite skips instead
# of failing.
SKIP_EXIT = 77


def dump(path):
    """Run ybrowser, return {data-test-name: (x, y, w, h)}."""
    res = subprocess.run(
        [YBROWSER, "--once", "--dump-boxes", "-w", WIDTH, path],
        capture_output=True,
        text=True,
        timeout=30,
        cwd=ROOT,
    )
    boxes = {}
    for line in res.stdout.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        cols = line.split("\t")
        if len(cols) < 7:
            continue
        name = cols[2]
        if name and name != "-":
            try:
                boxes[name] = (float(cols[3]), float(cols[4]), float(cols[5]), float(cols[6]))
            except ValueError:
                pass
    return boxes


def expects(path):
    """Pull {name: (x,y,w,h with None for *)} from data-test/data-expect attrs."""
    txt = open(path, encoding="utf-8").read()
    out = {}
    for pat in (
        r'data-test="([^"]+)"[^>]*?data-expect="([^"]+)"',
        r'data-expect="([^"]+)"[^>]*?data-test="([^"]+)"',
    ):
        for m in re.finditer(pat, txt):
            if pat.startswith(r'data-test'):
                name, spec = m.group(1), m.group(2)
            else:
                name, spec = m.group(2), m.group(1)
            fields = [p.strip() for p in spec.split(",")]
            while len(fields) < 4:
                fields.append("*")
            out[name] = tuple(_field(f) for f in fields[:4])
    return out


def _field(f):
    """`*` -> None (skip); `>=N`/`<=N` -> ('ge'|'le', N); else float (exact)."""
    if f == "*":
        return None
    if f.startswith(">="):
        return (">=", float(f[2:]))
    if f.startswith("<="):
        return ("<=", float(f[2:]))
    return float(f)


def _field_ok(got, want):
    if want is None:
        return True
    if isinstance(want, tuple):
        op, v = want
        return got >= v - TOL if op == ">=" else got <= v + TOL
    return abs(got - want) <= TOL


def match_ref(path):
    txt = open(path, encoding="utf-8").read()
    m = re.search(r'rel="match"\s+href="([^"]+)"', txt) or re.search(
        r'href="([^"]+)"\s+rel="match"', txt
    )
    return m.group(1) if m else None


def geo_eq(got, want):
    return all(_field_ok(g, w) for g, w in zip(got, want))


def run_test(path):
    boxes = dump(path)
    exp = expects(path)
    ref = match_ref(path)
    fails = []
    for name, want in exp.items():
        got = boxes.get(name)
        if got is None:
            fails.append(f"{name}: no box (display:none? not built?)")
        elif not geo_eq(got, want):
            fails.append(f"{name}: got {fmt(got)} expected {fmt(want)}")
    if ref:
        rboxes = dump(os.path.join(os.path.dirname(path), ref))
        shared = set(boxes) & set(rboxes)
        if not shared:
            fails.append("reftest: no shared data-test names between test and ref")
        for name in sorted(shared):
            if not geo_eq(boxes[name], rboxes[name]):
                fails.append(f"{name}: test {fmt(boxes[name])} != ref {fmt(rboxes[name])}")
    is_test = bool(exp or ref)
    return is_test, fails


def fmt(t):
    def one(v):
        if v is None:
            return "*"
        if isinstance(v, tuple):
            return f"{v[0]}{v[1]:g}"
        return f"{v:g}"
    return "(" + ",".join(one(v) for v in t) + ")"


def main():
    if not os.path.exists(YBROWSER):
        print(f"SKIP: ybrowser binary not found at {YBROWSER}")
        return SKIP_EXIT

    filters = sys.argv[1:]
    files = sorted(glob.glob(os.path.join(HERE, "**", "*.html"), recursive=True))
    passed = failed = skipped = xfailed = 0
    failures = []
    unexpected_pass = []
    for f in files:
        if f.endswith("-ref.html"):
            continue
        rel = os.path.relpath(f, HERE)
        if filters and not any(flt in rel for flt in filters):
            continue
        is_test, fails = run_test(f)
        if not is_test:
            skipped += 1
            continue
        if rel in KNOWN_FAILURES:
            # Kept out of the required-green tally either way. Surface an
            # unexpected pass so the entry can be retired, but never let it
            # fail the lane.
            xfailed += 1
            if fails:
                print(f"XFAIL {rel}")
            else:
                unexpected_pass.append(rel)
                print(f"XPASS {rel}  (now passing — remove from KNOWN_FAILURES)")
            continue
        if fails:
            failed += 1
            failures.append((rel, fails))
            print(f"FAIL  {rel}")
        else:
            passed += 1
            print(f"PASS  {rel}")
    print(f"\n=== {passed} passed, {failed} failed, {xfailed} xfail, {skipped} skipped ===")
    for rel, fails in failures:
        print(f"\n--- {rel}")
        for line in fails:
            print(f"    {line}")
    if unexpected_pass:
        print("\nXPASS (retire from KNOWN_FAILURES):")
        for rel in unexpected_pass:
            print(f"    {rel}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
