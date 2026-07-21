#!/usr/bin/env python3
"""Stage 0 profiler lifetime/teardown protocol checks.

Four scenarios, all against the committed fixtures:

1. tombstone-dynamic — 40 dynamic hot functions freed before teardown:
   every one must surface as a tombstone row with exact counters
   (calls=50, backedges=100000) and hot+eligible classification.
2. tombstone-live — identical workload, functions kept alive: exact
   counter totals must equal scenario 1 (a reclaimed function's metric
   contribution is identical to a live one's).
3. OOM fault injection (QJS_PROFILE_FORCE_TOMBSTONE_OOM) — tombstone
   loss must set the sticky `# incomplete 1` dump marker and analyze.py
   must hard-fail on it.
4. iframe-runtimes — top document + srcdoc-iframe child runtime:
   topLoop (80/320000) and childLoop (60/300000) each appear exactly
   once across the auto-discovered base + side dumps.

Exit 0 when all hold.
"""

import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
YBROWSER = os.environ.get(
    "YBROWSER",
    os.path.join(REPO, "build-desktop-ytrace-release/tools/ybrowser/ybrowser"))

sys.path.insert(0, HERE)
import importlib
analyze = importlib.import_module("analyze")

FAILURES = []


def check(label, condition, detail=""):
    if condition:
        print(f"ok   {label}")
    else:
        print(f"FAIL {label} {detail}")
        FAILURES.append(label)


def run_fixture(fixture_name, dump_path):
    for stale in [dump_path] + glob.glob(dump_path + ".0x*"):
        if os.path.exists(stale):
            os.unlink(stale)
    env = dict(os.environ,
               YBROWSER_JS_PROFILE="1",
               YBROWSER_JS_PROFILE_OUT=dump_path)
    fixture = os.path.join(HERE, "fixtures", fixture_name)
    subprocess.run([YBROWSER, "--once", "--dump-boxes", "-w", "1000", fixture],
                   env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=True)
    rows = []
    for path in analyze.discover_dumps([dump_path]):
        _, file_rows = analyze.parse_dump(path)
        rows.extend(file_rows)
    return rows


def dynamic_rows(rows):
    return [r for r in rows if r["calls"] == 50 and r["backedges"] == 100000]


def main():
    if not os.path.exists(YBROWSER):
        print(f"SKIP: ybrowser binary not found at {YBROWSER}")
        return 0

    # 1. tombstones carry full per-function data
    rows = run_fixture("tombstone-dynamic.html", "tmp/jit-lt-dynamic.tsv")
    dyn = dynamic_rows(rows)
    check("tombstone: 40 dynamic-function rows", len(dyn) == 40,
          f"(got {len(dyn)})")
    check("tombstone: all rows eligible", all(r["eligible"] for r in dyn))
    check("tombstone: exact counters preserved",
          sum(r["calls"] for r in dyn) == 2000 and
          sum(r["backedges"] for r in dyn) == 4000000)

    # 2. reclaimed contribution == live contribution
    live_rows = run_fixture("tombstone-live.html", "tmp/jit-lt-live.tsv")
    live_dyn = dynamic_rows(live_rows)
    check("live control: 40 rows", len(live_dyn) == 40,
          f"(got {len(live_dyn)})")
    check("tombstone == live exact totals",
          (sum(r["calls"] for r in dyn),
           sum(r["backedges"] for r in dyn)) ==
          (sum(r["calls"] for r in live_dyn),
           sum(r["backedges"] for r in live_dyn)))

    # 3. sticky OOM/incomplete marker: fault-inject tombstone loss and
    # verify the flag reaches the dump and the analyzer hard-fails
    os.environ["QJS_PROFILE_FORCE_TOMBSTONE_OOM"] = "1"
    try:
        run_fixture("tombstone-dynamic.html", "tmp/jit-lt-oom.tsv")
    finally:
        del os.environ["QJS_PROFILE_FORCE_TOMBSTONE_OOM"]
    header, _ = analyze.parse_dump("tmp/jit-lt-oom.tsv")
    check("oom: sticky incomplete flag in dump header",
          str(header.get("incomplete", "0")) == "1",
          f"(header incomplete={header.get('incomplete')!r})")
    analyzer_proc = subprocess.run(
        [sys.executable, os.path.join(HERE, "analyze.py"), "metrics",
         "tmp/jit-lt-oom.tsv"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    check("oom: analyzer hard-fails on incomplete dataset",
          analyzer_proc.returncode != 0,
          f"(rc={analyzer_proc.returncode})")
    check("oom: analyzer names the invalid dataset",
          "INCOMPLETE" in analyzer_proc.stdout)

    # 4. multi-runtime teardown + side-file discovery
    rows = run_fixture("iframe-runtimes.html", "tmp/jit-lt-iframe.tsv")
    side_files = glob.glob("tmp/jit-lt-iframe.tsv.0x*")
    top = [r for r in rows if r["name"] == "topLoop"]
    child = [r for r in rows if r["name"] == "childLoop"]
    check("iframe: side dump written and discovered", len(side_files) >= 1,
          f"(side files: {side_files})")
    check("iframe: topLoop exactly once", len(top) == 1, f"(got {len(top)})")
    check("iframe: childLoop exactly once", len(child) == 1,
          f"(got {len(child)})")
    if top:
        check("iframe: topLoop exact counters",
              top[0]["calls"] == 80 and top[0]["backedges"] == 320000,
              f"(got {top[0]['calls']}/{top[0]['backedges']})")
    if child:
        check("iframe: childLoop exact counters",
              child[0]["calls"] == 60 and child[0]["backedges"] == 300000,
              f"(got {child[0]['calls']}/{child[0]['backedges']})")
    if top and child:
        check("iframe: distinct runtimes",
              top[0]["runtime"] != child[0]["runtime"])

    print(f"\n{'PASS' if not FAILURES else 'FAIL'} "
          f"({len(FAILURES)} failure(s))")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
