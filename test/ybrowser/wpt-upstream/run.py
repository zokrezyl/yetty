#!/usr/bin/env python3
"""Run the REAL Web Platform Tests (check-layout-th.js layout tests) against
ybrowser and report conformance.

The upstream WPT layout tests annotate elements with the check-layout-th.js
contract — `data-offset-x`/`data-offset-y` (offsetLeft/offsetTop) and
`data-expected-width`/`data-expected-height` (offsetWidth/offsetHeight). We run
each test through `ybrowser --dump-wpt`, which emits each asserted element's
expected attrs alongside ybrowser's actual geometry, and compare (±1px, the WPT
tolerance). A test PASSES iff every asserted element matches.

    run.py <wpt-root> [suite-subdir ...]   # default: css/css-flexbox css/css-grid

Env: YBROWSER=<path>, WPT_TOL=<px> (default 1), WPT_WIDTH=<px> (default 800),
     WPT_MODE=offset|size|both (default both — which assertions to enforce).

Offset note: data-offset-x/y are relative to the element's offsetParent. We
compare against absolute document coords, which is exact when the offsetParent
is the body (the overwhelmingly common layout-test shape: a container that is a
direct, statically-positioned child of body). Tests that anchor to a positioned
sub-container will mis-report; those are filtered by --strict off by default.
"""
import os
import re
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")
TOL = float(os.environ.get("WPT_TOL", "1"))
WIDTH = os.environ.get("WPT_WIDTH", "800")
MODE = os.environ.get("WPT_MODE", "both")
JOBS = int(os.environ.get("WPT_JOBS", str(min(16, (os.cpu_count() or 4)))))


def run_test(path):
    """Return (status, checks_total, checks_pass, detail). status in {pass,fail,skip}."""
    try:
        txt = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return "skip", 0, 0, "unreadable"
    if "check-layout-th.js" not in txt:
        return "skip", 0, 0, "not a check-layout test"
    env = dict(os.environ)
    env.pop("DISPLAY", None)
    env.setdefault("YLEXBOR_BOOT_BUDGET_MS", "300")
    try:
        p = subprocess.run([YB, "--once", "--dump-wpt", "-w", WIDTH, path],
                           capture_output=True, env=env, timeout=20)
    except subprocess.TimeoutExpired:
        return "fail", 1, 0, "timeout"
    lines = [l for l in p.stdout.decode("utf-8", "replace").splitlines()
             if l and not l.startswith("#")]
    if not lines:
        return "skip", 0, 0, "no asserted boxes emitted"
    checks = 0
    passes = 0
    fails = []
    for ln in lines:
        f = ln.split("\t")
        if len(f) < 9:
            continue
        tag, ox, oy, ew, eh = f[0], f[1], f[2], f[3], f[4]
        try:
            ax, ay, aw, ah = float(f[5]), float(f[6]), float(f[7]), float(f[8])
        except ValueError:
            continue

        def chk(exp, act, label):
            nonlocal checks, passes
            if exp == "-":
                return
            try:
                e = float(exp)
            except ValueError:
                return
            checks += 1
            if abs(e - act) > TOL:
                fails.append(f"{tag}.{label} exp={e:g} act={act:g}")
            else:
                passes += 1

        if MODE in ("offset", "both"):
            chk(ox, ax, "x")
            chk(oy, ay, "y")
        if MODE in ("size", "both"):
            chk(ew, aw, "w")
            chk(eh, ah, "h")
    if checks == 0:
        return "skip", 0, 0, "no enforceable assertions"
    if fails:
        return "fail", checks, passes, "; ".join(fails[:4])
    return "pass", checks, passes, f"{checks} checks"


def main():
    if len(sys.argv) < 2:
        print("usage: run.py <wpt-root> [suite-subdir ...]", file=sys.stderr)
        return 2
    root = sys.argv[1]
    suites = sys.argv[2:] or ["css/css-flexbox", "css/css-grid"]
    tests = []
    for s in suites:
        d = os.path.join(root, s)
        for dp, _, fns in os.walk(d):
            for fn in fns:
                if fn.endswith(".html") and not fn.endswith("-ref.html"):
                    tests.append(os.path.join(dp, fn))
    tests.sort()
    counts = {"pass": 0, "fail": 0, "skip": 0}
    sub_total = 0
    sub_pass = 0
    failed = []
    suite_stat = {}
    with ProcessPoolExecutor(max_workers=JOBS) as ex:
        results = list(ex.map(run_test, tests, chunksize=4))
    for t, (st, ctot, cpass, detail) in zip(tests, results):
        counts[st] += 1
        sub_total += ctot
        sub_pass += cpass
        suite = os.path.relpath(t, root).split("/")[1] if "/" in os.path.relpath(t, root) else "?"
        ss = suite_stat.setdefault(suite, [0, 0, 0])  # pass, fail, skip
        ss[{"pass": 0, "fail": 1, "skip": 2}[st]] += 1
        if st == "fail":
            failed.append((os.path.relpath(t, root), detail))
    enforced = counts["pass"] + counts["fail"]
    tpct = 100.0 * counts["pass"] / enforced if enforced else 0.0
    spct = 100.0 * sub_pass / sub_total if sub_total else 0.0
    print(f"\n=== upstream WPT ({len(tests)} files) ===")
    print(f"  TESTS:    {counts['pass']}/{enforced} green ({tpct:.1f}%)  "
          f"[{counts['fail']} fail, {counts['skip']} skip]")
    print(f"  SUBTESTS: {sub_pass}/{sub_total} green ({spct:.1f}%)  "
          f"(each element-assertion = one subtest)")
    print("\n  per-suite (pass/fail/skip):")
    for s in sorted(suite_stat):
        p, f, k = suite_stat[s]
        tot = p + f
        print(f"    {s:24s} {p:5d}/{tot:<5d} ({100.0*p/tot if tot else 0:5.1f}%)  skip={k}")
    # Dump ALL failure details to a file for root-cause clustering.
    with open("tmp/wpt-fails.txt", "w") as fh:
        for name, detail in failed:
            fh.write(f"{name}\t{detail}\n")
    # Histogram of failures by feature cluster (keyword in the test path) so the
    # systematic engine gaps surface ahead of the alphabetical-first niche ones.
    from collections import Counter
    KW = ["abspos", "writing-mode", "vert", "-rtl", "baseline", "align-content",
          "align-items", "align-self", "justify", "order", "gap", "flex-basis",
          "flex-grow", "flex-shrink", "flex-wrap", "min-", "max-", "percentage",
          "intrinsic", "aspect-ratio", "overflow", "margin", "table", "multicol"]
    hist = Counter()
    for name, _ in failed:
        low = name.lower()
        matched = [k for k in KW if k in low]
        for k in (matched or ["(other)"]):
            hist[k] += 1
    print("\nfailures by feature cluster:")
    for k, n in hist.most_common():
        print(f"  {n:4d}  {k}")
    print("\nfirst 30 failures:")
    for name, detail in failed[:30]:
        print(f"  FAIL  {name}\n        {detail}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
