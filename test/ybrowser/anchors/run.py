#!/usr/bin/env python3
"""
Anchor-suite runner: for every fixtures/<name>/ (fixture.html + ref.json),
render the fixture with `ybrowser --dump-boxes` at the reference's viewport
width and compare anchor geometry against the committed Chrome reference.

    run.py                # all fixtures
    run.py flex grid      # only fixtures whose name contains a filter string

Env:
    YBROWSER   path to the ybrowser binary
               (default build-desktop-ytrace-release/tools/ybrowser/ybrowser)

Headless: DISPLAY is dropped from the environment; no GPU, no network (the
committed fixtures are self-contained). Exit 77 (ctest SKIP) when the binary
is missing.
"""
import os
import subprocess
import sys

import compare

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
YBROWSER = os.environ.get(
    "YBROWSER", os.path.join(ROOT, "build-desktop-ytrace-release",
                             "tools", "ybrowser", "ybrowser"))
SKIP_EXIT = 77
FINDINGS_SHOWN = 12

# Fixtures with triaged, tracked divergences — reported but not counted as
# failures, so the suite stays a ratchet: a NEW fixture regression fails the
# lane while a known engine gap is worked on. Keep every entry commented with
# what diverges; retire the entry when it goes green (the runner flags that).
KNOWN_FAILURES = {
    # #458's engine gaps (stylesheet flex shorthand, shrink-to-fit under a
    # distributing justify-content, flex:2 grow ratio) are fixed; what
    # remains is font-metric drift: the naive mono glyph advance wraps
    # paragraphs one line earlier/later than Chrome's sans-serif metrics
    # (nav Δ27px wide, wrapped <p> heights Δ~30px, y-offsets downstream).
    "flex-cards",
    # #458's gaps (stylesheet `grid-column: span 2`, grid-item stretch to
    # row height) are fixed; the two remaining findings are the same
    # font-metric wrap drift on the banner paragraph (Δ25px height).
    "grid-sidebar",
}


def run_fixture(name, directory):
    ref_path = os.path.join(directory, "ref.json")
    fixture_path = os.path.join(directory, "fixture.html")
    if not (os.path.exists(ref_path) and os.path.exists(fixture_path)):
        return None
    import json
    reference = json.load(open(ref_path, encoding="utf-8"))
    width = str(int(reference["meta"]["width"]))

    environment = dict(os.environ)
    environment.pop("DISPLAY", None)
    environment.setdefault("YLEXBOR_BOOT_BUDGET_MS", "500")
    result = subprocess.run(
        [YBROWSER, "--once", "--dump-boxes", "-w", width, fixture_path],
        capture_output=True, text=True, timeout=60, cwd=ROOT,
        env=environment)
    if result.returncode != 0:
        print("FAIL  %s  (ybrowser exited %d)" % (name, result.returncode))
        sys.stdout.write(result.stderr[-1500:])
        return False

    comparison = compare.compare_texts(reference, result.stdout)
    stats = comparison.stats
    ok = not comparison.findings
    known = name in KNOWN_FAILURES
    if ok:
        verdict = "XPASS" if known else "PASS "
        print("%s %s  (%d anchors)" % (verdict, name, stats["anchors"]))
        if known:
            print("      now green — retire from KNOWN_FAILURES")
    else:
        verdict = "XFAIL" if known else "FAIL "
        print("%s %s  (%d anchors: %d missing, %d structure, %d geometry)"
              % (verdict, name, stats["anchors"], stats["missing"],
                 stats["structure"], stats["geometry"]))
        compare.report(comparison, FINDINGS_SHOWN)
    return ok or known


def main():
    if not os.path.exists(YBROWSER):
        print("SKIP: ybrowser binary not found at %s" % YBROWSER)
        return SKIP_EXIT

    filters = sys.argv[1:]
    fixtures_dir = os.path.join(HERE, "fixtures")
    names = sorted(os.listdir(fixtures_dir)) if os.path.isdir(fixtures_dir) \
        else []
    passed = failed = 0
    for name in names:
        directory = os.path.join(fixtures_dir, name)
        if not os.path.isdir(directory):
            continue
        if filters and not any(flt in name for flt in filters):
            continue
        outcome = run_fixture(name, directory)
        if outcome is None:
            continue
        if outcome:
            passed += 1
        else:
            failed += 1
    print("\n=== %d passed, %d failed ===" % (passed, failed))
    if passed + failed == 0:
        print("no fixtures matched")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
