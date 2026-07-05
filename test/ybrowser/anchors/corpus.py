#!/usr/bin/env python3
"""
Live-site anchor corpus runner: capture -> render -> compare, in one
command, across the whole list of tracked sites — the fast loop for
"which pages regressed and what mechanism broke".

    corpus.py                 # render+compare every captured page (parallel)
    corpus.py gnews           # only pages whose name contains a filter
    corpus.py --capture       # (re)capture missing pages first (needs
                              # Chrome + network); with --recapture, all
    corpus.py --limit 6       # findings shown per page (default 3)

The page list lives in corpus.txt next to this script (`name width url`
per line; # comments). Captures are live-site content, so they live under
tmp/anchors-live/<name>/ — never in fixtures/ (see README.md). Renders
run in parallel; each page's box dump is kept at
tmp/anchors-live/<name>/dump.tsv so inspect.py can drill in immediately:

    inspect.py tmp/anchors-live/gnews-home a0042      # anchor detail
    inspect.py tmp/anchors-live/gnews-home --children a0042

Every run prints, per page: missing/structure/geometry counts with the
DELTA vs the previous run (persisted in tmp/anchors-live/corpus-status.json)
and the top mechanism clusters — so a regression and its shape are visible
in one screen without reading per-anchor findings.
"""
import argparse
import concurrent.futures
import json
import os
import subprocess
import sys

import compare

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
YBROWSER = os.environ.get(
    "YBROWSER", os.path.join(ROOT, "build-desktop-ytrace-release",
                             "tools", "ybrowser", "ybrowser"))
LIVE_DIR = os.path.join(ROOT, "tmp", "anchors-live")
STATUS_PATH = os.path.join(LIVE_DIR, "corpus-status.json")
MANIFEST_PATH = os.path.join(HERE, "corpus.txt")


def read_manifest():
    pages = []
    if not os.path.exists(MANIFEST_PATH):
        return pages
    for line in open(MANIFEST_PATH, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 2)
        if len(parts) != 3:
            print("corpus.txt: skipping malformed line: %s" % line,
                  file=sys.stderr)
            continue
        pages.append({"name": parts[0], "width": int(parts[1]),
                      "url": parts[2]})
    return pages


def capture_page(page, recapture):
    directory = os.path.join(LIVE_DIR, page["name"])
    have = (os.path.exists(os.path.join(directory, "ref.json"))
            and os.path.exists(os.path.join(directory, "fixture.html")))
    if have and not recapture:
        return True
    print("capturing %s (%s)" % (page["name"], page["url"]))
    result = subprocess.run(
        [sys.executable, os.path.join(HERE, "capture.py"), page["url"],
         directory, "-w", str(page["width"])],
        capture_output=True, text=True, timeout=300)
    if result.returncode != 0:
        print("capture FAILED for %s:\n%s" % (page["name"],
                                              result.stderr[-1000:]),
              file=sys.stderr)
        return False
    return True


def render_page(page):
    """Render one captured page; returns (page_name, comparison|None, err)."""
    directory = os.path.join(LIVE_DIR, page["name"])
    ref_path = os.path.join(directory, "ref.json")
    fixture_path = os.path.join(directory, "fixture.html")
    if not (os.path.exists(ref_path) and os.path.exists(fixture_path)):
        return page["name"], None, "not captured (run with --capture)"
    reference = json.load(open(ref_path, encoding="utf-8"))
    width = str(int(reference["meta"]["width"]))

    environment = dict(os.environ)
    environment.pop("DISPLAY", None)
    environment.setdefault("YLEXBOR_BOOT_BUDGET_MS", "500")
    dump_path = os.path.join(directory, "dump.tsv")
    with open(dump_path, "w", encoding="utf-8") as dump_file:
        result = subprocess.run(
            [YBROWSER, "--once", "--dump-boxes", "-w", width, fixture_path],
            stdout=dump_file, stderr=subprocess.PIPE, text=True,
            timeout=180, cwd=ROOT, env=environment)
    if result.returncode != 0:
        return page["name"], None, ("ybrowser exited %d: %s"
                                    % (result.returncode,
                                       result.stderr[-300:]))
    dump_text = open(dump_path, encoding="utf-8", errors="replace").read()
    comparison = compare.compare_texts(reference, dump_text,
                                       compare.load_classes(fixture_path))
    return page["name"], comparison, None


def load_status():
    try:
        return json.load(open(STATUS_PATH, encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def delta_str(current, previous):
    if previous is None:
        return "%5d      " % current
    diff = current - previous
    mark = "" if diff == 0 else ("+%d" % diff if diff > 0 else str(diff))
    return "%5d %-5s" % (current, ("(%s)" % mark) if mark else "")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("filters", nargs="*",
                        help="only pages whose name contains a filter")
    parser.add_argument("--capture", action="store_true",
                        help="capture pages missing from tmp/anchors-live "
                             "(needs Chrome + network)")
    parser.add_argument("--recapture", action="store_true",
                        help="recapture every selected page")
    parser.add_argument("--limit", type=int, default=3,
                        help="findings shown per page (default 3)")
    parser.add_argument("--clusters", type=int, default=6,
                        help="cluster lines shown per page (default 6)")
    parser.add_argument("--jobs", type=int,
                        default=max(2, (os.cpu_count() or 4) // 2))
    args = parser.parse_args()

    if not os.path.exists(YBROWSER):
        print("error: ybrowser binary not found at %s" % YBROWSER,
              file=sys.stderr)
        return 2
    pages = read_manifest()
    if args.filters:
        pages = [page for page in pages
                 if any(flt in page["name"] for flt in args.filters)]
    if not pages:
        print("no pages selected (see %s)" % MANIFEST_PATH)
        return 2

    if args.capture or args.recapture:
        # Captures run sequentially — each spawns its own Chrome.
        pages = [page for page in pages
                 if capture_page(page, args.recapture)]

    previous_status = load_status()
    status = dict(previous_status)
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(render_page, page) for page in pages]
        for future in futures:
            results.append(future.result())

    print("%-16s %-10s %-11s %-11s %s" %
          ("page", "anchors", "missing", "structure", "geometry"))
    failures = 0
    for name, comparison, error in results:
        if comparison is None:
            print("%-16s ERROR: %s" % (name, error))
            failures += 1
            continue
        stats = comparison.stats
        prev = previous_status.get(name, {})
        print("%-16s %-10d %s %s %s" %
              (name, stats["anchors"],
               delta_str(stats["missing"], prev.get("missing")),
               delta_str(stats["structure"], prev.get("structure")),
               delta_str(stats["geometry"], prev.get("geometry"))))
        status[name] = {"missing": stats["missing"],
                        "structure": stats["structure"],
                        "geometry": stats["geometry"],
                        "anchors": stats["anchors"]}

    for name, comparison, error in results:
        if comparison is None or not comparison.findings:
            continue
        print("\n== %s" % name)
        compare.report(comparison, args.limit, args.clusters)

    os.makedirs(LIVE_DIR, exist_ok=True)
    with open(STATUS_PATH, "w", encoding="utf-8") as handle:
        json.dump(status, handle, indent=1, sort_keys=True)
        handle.write("\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
