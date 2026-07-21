#!/usr/bin/env python3
"""Stage 0 JIT-opportunity analysis over qjs-profile histogram dumps (v2).

Input: the TSV(s) written by the engine profiler (YBROWSER_JS_PROFILE=1,
YBROWSER_JS_PROFILE_OUT=<path>; see src/quickjs/quickjs-jit.h). The
profile is thread-wide: the owner runtime writes <path> and each iframe
child runtime writes <path>.<runtime> — pass ALL of them to `metrics`
(rows are drained on dump, so files never double-count).

Metrics (registered in tmp/qjs-sljit-jit.md — do not change after data):

  hot(f)      = calls >= N_call OR (backedges >= N_backedge AND calls >= 2)
  self(f)     = dispatch + prop_load + prop_write + call + string + vm
                + unknown samples (native-callee samples excluded)
  eligible_hot_share  = sum(self(f) : hot and eligible) / sum(self(f))
  stage3_page_opportunity      = eligible-hot dispatch seconds / wall seconds
  stage4_load_page_opportunity = (… + eligible-hot prop_load seconds) / wall
  page_opportunity             = eligible-hot self seconds / wall seconds

Native (DOM/WebAPI + engine builtin callee) time is reported separately:
it is outside every opportunity numerator but part of the page picture.
UNKNOWN samples (opcodes without a conscious stage-3 classification)
fail the run when their eligible-hot share exceeds --max-unknown-share.

Side files (`<path>.<runtime>` written by iframe child runtimes) are
discovered automatically for every path passed to `metrics`. Rows carry
their runtime identity (v3 format); tombstone rows for functions freed
before teardown are flushed into the dumps and merge like live rows.

Usage:
  analyze.py metrics DUMP.tsv [more.tsv …] [--wall-seconds W]
             [--n-call 32] [--n-backedge 1000] [--max-unknown-share 0.02]
  analyze.py repeat DUMP1.tsv DUMP2.tsv [DUMP3.tsv …]

`repeat` verifies exact-counter repeatability across runs of a
deterministic fixture: per-function call/backedge counts must be
identical run to run (sampling columns are expected to vary).
"""

import argparse
import glob as globmod
import sys

CATS = ["dispatch", "prop_load", "prop_write", "call", "string", "vm",
        "native", "unknown"]
SELF_CATS = [c for c in CATS if c != "native"]


def parse_dump(path):
    header = {}
    rows = []
    with open(path) as dump_file:
        for line in dump_file:
            line = line.rstrip("\n")
            if line.startswith("#"):
                parts = line[1:].strip().split("\t")
                header[parts[0]] = parts[1:] if len(parts) > 2 else (
                    parts[1] if len(parts) == 2 else "")
                continue
            if line.startswith("name\t"):
                continue
            fields = line.split("\t")
            if len(fields) < 17:
                continue
            row = {
                "name": fields[0],
                "source": fields[1],
                "bc_size": int(fields[2]),
                "func_kind": int(fields[3]),
                "calls": int(fields[4]),
                "backedges": int(fields[5]),
                "eligible": fields[14] == "1",
                "reason": fields[15],
                "runtime": fields[16],
            }
            for cat_index, cat in enumerate(CATS):
                row["s_" + cat] = int(fields[6 + cat_index])
            rows.append(row)
    return header, rows


def self_samples(row):
    return sum(row["s_" + cat] for cat in SELF_CATS)


def discover_dumps(paths):
    """Each base path plus its `<base>.0x…` child-runtime side files."""
    seen = []
    for path in paths:
        for candidate in [path] + sorted(globmod.glob(path + ".0x*")):
            if candidate not in seen:
                seen.append(candidate)
    return seen


def cmd_metrics(args):
    headers = []
    rows = []
    for path in discover_dumps(args.dumps):
        header, file_rows = parse_dump(path)
        headers.append((path, header))
        rows.extend(file_rows)

    main_header = headers[0][1]
    hz = int(main_header.get("sample_hz", 1000))
    # Thread-wide totals: every dump snapshots the same counters; the
    # largest samples_total is the latest/most complete snapshot.
    samples_total = max(int(h.get("samples_total", 0)) for _, h in headers)
    no_frame = max(int(h.get("samples_no_frame", 0)) for _, h in headers)

    def hot(row):
        return (row["calls"] >= args.n_call or
                (row["backedges"] >= args.n_backedge and row["calls"] >= 2))

    incomplete = [path for path, h in headers
                  if str(h.get("incomplete", "0")) == "1"]
    if incomplete:
        print("FAIL: dataset INCOMPLETE — tombstone loss (OOM) in: "
              + ", ".join(incomplete))
        print("per-function data is not trustworthy; the run is invalid "
              "for any go/no-go purpose")
        return 4

    total_self = sum(self_samples(r) for r in rows)
    total_native = sum(r["s_native"] for r in rows)
    hot_eligible = [r for r in rows if hot(r) and r["eligible"]]
    he_self = sum(self_samples(r) for r in hot_eligible)
    he_dispatch = sum(r["s_dispatch"] for r in hot_eligible)
    he_prop_load = sum(r["s_prop_load"] for r in hot_eligible)
    he_unknown = sum(r["s_unknown"] for r in hot_eligible)

    print(f"dumps                {', '.join(args.dumps)}")
    print(f"fingerprint          {main_header.get('fingerprint', '?')}")
    print(f"sample_hz            {hz}")
    print(f"functions            {len(rows)}  "
          f"(hot+eligible: {len(hot_eligible)})")
    print(f"thresholds           N_call={args.n_call} "
          f"N_backedge={args.n_backedge}")
    print(f"JS self samples      {total_self} ({total_self / hz:.3f}s)")
    print(f"native-callee samples {total_native} ({total_native / hz:.3f}s)"
          f"  [outside opportunity numerators]")
    print(f"thread samples total {samples_total} "
          f"(no-JS-frame: {no_frame})")
    for path, header in headers:
        if header.get("reclaimed"):
            print(f"reclaimed {path}: {header['reclaimed']}")

    if total_self == 0:
        print("no JS self samples — nothing to analyze")
        return 1

    share = he_self / total_self
    print(f"eligible_hot_share   {share:.1%}")

    by_cat = {cat: sum(r["s_" + cat] for r in hot_eligible)
              for cat in CATS}
    print("eligible-hot by category: " + "  ".join(
        f"{cat}={by_cat[cat]}" for cat in CATS))

    unknown_share = (he_unknown / he_self) if he_self else 0.0
    print(f"unknown share of eligible-hot self  {unknown_share:.2%} "
          f"(tolerance {args.max_unknown_share:.2%})")

    status = 0
    if unknown_share > args.max_unknown_share:
        print("FAIL: unclassified eligible-hot time exceeds tolerance — "
              "classify the missing opcodes before using this data")
        status = 2

    if args.wall_seconds:
        wall = args.wall_seconds
        stage3 = he_dispatch / hz / wall
        stage4 = (he_dispatch + he_prop_load) / hz / wall
        ceiling = he_self / hz / wall
        js_share = total_self / hz / wall
        native_share = total_native / hz / wall

        def amdahl(x):
            return 1.0 / (1.0 - x) if x < 1.0 else float("inf")

        print(f"wall seconds         {wall:.3f}")
        print(f"  JS self share of wall     {js_share:.1%}")
        print(f"  native-callee share       {native_share:.1%}")
        print(f"stage3_page_opportunity       {stage3:.2%}  "
              f"(Amdahl bound x{amdahl(stage3):.3f})")
        print(f"stage4_load_page_opportunity  {stage4:.2%}  "
              f"(Amdahl bound x{amdahl(stage4):.3f})")
        print(f"page_opportunity (ceiling)    {ceiling:.2%}  "
              f"(Amdahl bound x{amdahl(ceiling):.3f})")
    else:
        print("(pass --wall-seconds for the page-level opportunity bounds)")

    if args.top:
        ranked = sorted(rows, key=self_samples, reverse=True)[:args.top]
        print(f"\ntop {args.top} by self samples:")
        for r in ranked:
            flags = ("HOT" if hot(r) else "cold") + \
                    ("+ELIG" if r["eligible"] else f"+{r['reason']}")
            print(f"  {self_samples(r):6d}  {r['name'][:40]:40s} "
                  f"{r['source'][:40]:40s} calls={r['calls']} "
                  f"backedges={r['backedges']} [{flags}]")
    return status


def cmd_repeat(args):
    baseline = None
    baseline_path = None
    status = 0
    for path in args.dumps:
        _, rows = parse_dump(path)
        counters = {}
        for r in rows:
            key = (r["name"], r["source"], r["bc_size"])
            counters[key] = (r["calls"], r["backedges"],
                             counters.get(key, (0, 0, 0))[2] + 1)
        if baseline is None:
            baseline, baseline_path = counters, path
            continue
        mismatches = []
        for key in sorted(set(baseline) | set(counters)):
            if baseline.get(key) != counters.get(key):
                mismatches.append(
                    f"  {key}: {baseline_path}={baseline.get(key)} "
                    f"{path}={counters.get(key)}")
        if mismatches:
            status = 1
            print(f"MISMATCH {baseline_path} vs {path}:")
            print("\n".join(mismatches[:20]))
            if len(mismatches) > 20:
                print(f"  … {len(mismatches) - 20} more")
        else:
            print(f"OK exact counters identical: {baseline_path} vs {path}")
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    metrics = sub.add_parser("metrics")
    metrics.add_argument("dumps", nargs="+")
    metrics.add_argument("--wall-seconds", type=float, default=None)
    metrics.add_argument("--n-call", type=int, default=32)
    metrics.add_argument("--n-backedge", type=int, default=1000)
    metrics.add_argument("--max-unknown-share", type=float, default=0.02)
    metrics.add_argument("--top", type=int, default=10)
    metrics.set_defaults(func=cmd_metrics)

    repeat = sub.add_parser("repeat")
    repeat.add_argument("dumps", nargs="+")
    repeat.set_defaults(func=cmd_repeat)

    args = parser.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
