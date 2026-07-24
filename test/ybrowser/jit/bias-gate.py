#!/usr/bin/env python3
"""Paired profiled/unprofiled sampling-bias gate (registered in the JIT
design): profiling overhead must be <= 2% at p50 and <= 5% at p95 of
wall time, with >= 10k JS CPU samples per workload for admissible gate
data. Runs are interleaved (u,p,u,p,…) to decorrelate machine drift.

Usage:
  bias-gate.py <url-or-file> [--reps 10] [--width 1280] [--hz 1000]
               [--min-samples 10000]

Exit 0 = gate passes (or sample floor not applicable for fixtures with
--min-samples 0); exit 1 = overhead gate failed; exit 3 = sample floor
not met (data inadmissible, but overhead verdict still printed).
"""

import argparse
import glob
import os
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
YBROWSER = os.environ.get(
    "YBROWSER",
    os.path.join(REPO, "build-desktop-ytrace-release/tools/ybrowser/ybrowser"))


def run_once(target, width, profile, hz, dump_path):
    env = dict(os.environ)
    env.pop("YBROWSER_JS_PROFILE", None)
    env.pop("YBROWSER_PROFILE", None)
    if profile:
        env["YBROWSER_JS_PROFILE"] = "1"
        env["YBROWSER_JS_PROFILE_HZ"] = str(hz)
        env["YBROWSER_JS_PROFILE_OUT"] = dump_path
        for stale in [dump_path] + glob.glob(dump_path + ".0x*"):
            if os.path.exists(stale):
                os.unlink(stale)
    start = time.monotonic()
    subprocess.run([YBROWSER, "--once", "--dump-boxes", "-w", str(width),
                    target],
                   env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=True)
    return time.monotonic() - start


def total_samples(dump_path):
    best = 0
    for path in [dump_path] + glob.glob(dump_path + ".0x*"):
        if not os.path.exists(path):
            continue
        with open(path) as dump_file:
            for line in dump_file:
                if line.startswith("# samples_total"):
                    best = max(best, int(line.split("\t")[1]))
    return best


def percentile(values, fraction):
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1))))
    return ordered[index]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target")
    parser.add_argument("--reps", type=int, default=10)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--hz", type=int, default=1000)
    parser.add_argument("--min-samples", type=int, default=10000)
    args = parser.parse_args()

    if not os.path.exists(YBROWSER):
        print(f"SKIP: ybrowser binary not found at {YBROWSER}")
        return 0

    dump_path = "tmp/jit-bias-gate.tsv"
    unprofiled, profiled, samples = [], [], []

    # warmup (caches, page data) — one run of each, discarded
    run_once(args.target, args.width, False, args.hz, dump_path)
    run_once(args.target, args.width, True, args.hz, dump_path)

    for rep in range(args.reps):
        unprofiled.append(run_once(args.target, args.width, False,
                                   args.hz, dump_path))
        profiled.append(run_once(args.target, args.width, True,
                                 args.hz, dump_path))
        samples.append(total_samples(dump_path))

    p50_u, p95_u = percentile(unprofiled, .5), percentile(unprofiled, .95)
    p50_p, p95_p = percentile(profiled, .5), percentile(profiled, .95)
    d50 = (p50_p - p50_u) / p50_u
    d95 = (p95_p - p95_u) / p95_u
    median_samples = statistics.median(samples)

    print(f"target        {args.target}")
    print(f"reps          {args.reps} paired (interleaved), 1 warmup pair")
    print(f"unprofiled    p50={p50_u:.3f}s p95={p95_u:.3f}s")
    print(f"profiled      p50={p50_p:.3f}s p95={p95_p:.3f}s")
    print(f"overhead      p50={d50:+.2%} (gate <=2%)  p95={d95:+.2%} (gate <=5%)")
    print(f"JS CPU samples median {median_samples:.0f} "
          f"(floor {args.min_samples})")

    status = 0
    if d50 > 0.02 or d95 > 0.05:
        print("FAIL: overhead gate exceeded — reduce mutator-side tracking "
              "cost (lowering the timer rate alone does not fix the per-op "
              "tap cost)")
        status = 1
    else:
        print("overhead gate: PASS")
    if args.min_samples and median_samples < args.min_samples:
        print("sample floor NOT met — data inadmissible for the go/no-go "
              "gate (lengthen the run or raise activity)")
        status = status or 3
    return status


if __name__ == "__main__":
    sys.exit(main())
