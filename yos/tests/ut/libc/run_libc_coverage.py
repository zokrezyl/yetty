#!/usr/bin/env python3
"""Run test_libc_coverage.wasm under yos, parse FN: lines, write a
markdown coverage table.

Each FN line in stdout is one of:
    FN:<name>:PASS
    FN:<name>:FAIL:<note>
    FN:<name>:SKIP:<reason>

We:
  1. Run yos <wasm>.
  2. Parse all FN: lines.
  3. Cross-check against the full bridged-function list (extracted
     from build-linux/src/yos/codegen/yos_bridge.h) so the table
     also shows which bridged functions were NOT exercised.
  4. Write tmp/libc-coverage.md (table) and tmp/libc-coverage.json
     (machine-readable) in the project root.
  5. Exit 0 if the run completed (COVERAGE-DONE marker present),
     1 if the run aborted before finishing — per-function FAILs are
     visible in the table but do NOT fail the test, so the whole
     surface gets reported in one go instead of stopping at the
     first broken bridge.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


FN_RE = re.compile(r"^FN:([^:]+):(PASS|FAIL|SKIP)(?::(.*))?$")


def all_bridged(repo: Path, build_dir: str) -> list[str]:
    """Extract every yos_<name> bridge entry from the codegen header."""
    h = repo / build_dir / "src" / "yos" / "codegen" / "yos_bridge.h"
    if not h.exists():
        return []
    seen = set()
    for line in h.read_text().splitlines():
        m = re.search(r"\byos_([A-Za-z_][A-Za-z0-9_]*)\s*\(", line)
        if not m:
            continue
        name = m.group(1)
        if name == "brg_link_imports":
            continue
        seen.add(name)
    return sorted(seen)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--yos",  required=True, type=Path)
    p.add_argument("--src",  required=True, type=Path)
    p.add_argument("--wasm", required=True, type=Path)
    p.add_argument("--repo", required=True, type=Path)
    p.add_argument("--build-dir", default="build-linux")
    p.add_argument("--timeout", type=int, default=30)
    args = p.parse_args()

    proc = subprocess.run(
        [str(args.yos), str(args.wasm)],
        capture_output=True, text=True, timeout=args.timeout,
    )

    pass_set: set[str] = set()
    fail_map: dict[str, str] = {}
    skip_map: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        m = FN_RE.match(line.strip())
        if not m:
            continue
        name, status, note = m.group(1), m.group(2), m.group(3) or ""
        # Strip ":subtag" (e.g. "fcntl:F_GETFD") to get the base name.
        base = name.split(":", 1)[0]
        if status == "PASS":
            pass_set.add(base)
        elif status == "FAIL":
            fail_map.setdefault(base, note)
        else:
            skip_map.setdefault(base, note)

    done = ("COVERAGE-DONE" in proc.stdout) or ("COVERAGE-DONE-RAW" in proc.stdout)

    bridged = all_bridged(args.repo, args.build_dir)
    bridged_set = set(bridged)
    covered = pass_set | set(fail_map.keys()) | set(skip_map.keys())
    untested = sorted(bridged_set - covered)
    extra    = sorted(covered - bridged_set)  # tested but not in bridge.h

    n_total   = len(bridged)
    n_pass    = len(pass_set & bridged_set)
    n_fail    = len(set(fail_map) & bridged_set)
    n_skip    = len(set(skip_map) & bridged_set)
    n_unt     = len(untested)
    pct_cov   = (100.0 * (n_pass + n_fail + n_skip) / n_total) if n_total else 0.0
    pct_pass  = (100.0 * n_pass / n_total) if n_total else 0.0

    out_dir = args.repo / "tmp"
    out_dir.mkdir(parents=True, exist_ok=True)
    md = out_dir / "libc-coverage.md"
    js = out_dir / "libc-coverage.json"

    lines: list[str] = []
    lines.append("# yos libc coverage")
    lines.append("")
    lines.append(f"- Total bridged functions: **{n_total}**")
    lines.append(f"- Probed (PASS+FAIL+SKIP):  **{n_pass + n_fail + n_skip}** ({pct_cov:.1f}%)")
    lines.append(f"- PASS:                     **{n_pass}** ({pct_pass:.1f}%)")
    lines.append(f"- FAIL:                     **{n_fail}**")
    lines.append(f"- SKIP:                     **{n_skip}**")
    lines.append(f"- Untested:                 **{n_unt}**")
    lines.append(f"- COVERAGE-DONE marker:     {'yes' if done else '**NO** (run aborted)'}")
    lines.append("")

    if fail_map:
        lines.append("## Failures")
        lines.append("")
        lines.append("| function | failure |")
        lines.append("|---|---|")
        for k in sorted(fail_map):
            lines.append(f"| `{k}` | {fail_map[k]} |")
        lines.append("")

    if skip_map:
        lines.append("## Skips")
        lines.append("")
        lines.append("| function | reason |")
        lines.append("|---|---|")
        for k in sorted(skip_map):
            lines.append(f"| `{k}` | {skip_map[k]} |")
        lines.append("")

    if pass_set:
        lines.append("## Passes")
        lines.append("")
        # Compact: just a sorted list, comma-separated, wrapped.
        items = sorted(pass_set)
        chunk = ", ".join(f"`{x}`" for x in items)
        lines.append(chunk)
        lines.append("")

    if untested:
        lines.append(f"## Untested ({n_unt} bridged but no probe yet)")
        lines.append("")
        # Compact list — too many to make a table reasonable.
        chunk = ", ".join(f"`{x}`" for x in untested)
        lines.append(chunk)
        lines.append("")

    if extra:
        lines.append("## Probed but not in bridge.h (subtags)")
        lines.append("")
        lines.append(", ".join(f"`{x}`" for x in extra))
        lines.append("")

    md.write_text("\n".join(lines) + "\n")
    js.write_text(json.dumps({
        "total_bridged": n_total,
        "passed":   sorted(pass_set),
        "failed":   fail_map,
        "skipped":  skip_map,
        "untested": untested,
        "extra":    extra,
        "done":     done,
    }, indent=2) + "\n")

    print(f"libc coverage: total_bridged={n_total} pass={n_pass} "
          f"fail={n_fail} skip={n_skip} untested={n_unt} "
          f"covered={pct_cov:.1f}% passing={pct_pass:.1f}%")
    print(f"  table: {md}")
    print(f"  json:  {js}")

    if not done:
        print("FAIL: COVERAGE-DONE marker missing — wasm aborted before finishing")
        if proc.stderr:
            print(f"  stderr (tail 500): {proc.stderr[-500:]!r}")
        if proc.stdout:
            print(f"  stdout (tail 500): {proc.stdout[-500:]!r}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
