#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""check-format.py — report files that don't match .clang-format.

Exit codes:
  0  all files match
  1  at least one file needs reformatting
  2  clang-format not installed / setup problem
"""

from __future__ import annotations

import argparse
import difflib
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from _common import (  # noqa: E402
    TMP_DIR,
    CheckResult,
    err,
    info,
    list_sources,
    ok,
    pick_tool,
    rel,
    run,
    scope_paths_from_env,
    warn,
)

CLANG_FORMAT_CANDIDATES = (
    "clang-format-20",
    "clang-format-21",
    "clang-format-18",
    "clang-format-19",
    "clang-format-17",
    "clang-format-15",
    "clang-format-14",
    "clang-format-9",
)


def check_format(paths: list[str] | None = None) -> CheckResult:
    started = time.time()
    binary = os.environ.get("CLANG_FORMAT") or pick_tool(*CLANG_FORMAT_CANDIDATES)
    if not binary:
        err("no clang-format binary found")
        return CheckResult(
            name="clang-format", status="fail",
            summary="clang-format not installed",
            duration_s=time.time() - started,
        )

    version = run([binary, "--version"]).stdout.strip().splitlines()[0]
    info(f"using {binary} ({version})")

    files = list_sources(paths)
    if not files:
        warn("no source files found")
        return CheckResult(name="clang-format", status="pass",
                           summary="no files", duration_s=time.time() - started)

    diff_file = TMP_DIR / "check-format.diff"
    per_file_path = TMP_DIR / "check-format.per-file.txt"
    diff_file.write_text("")

    bad: list[tuple[int, str]] = []  # (changed_lines, relpath)
    diff_parts: list[str] = []

    for f in files:
        original = f.read_text(errors="replace").splitlines(keepends=True)
        formatted_proc = run([binary, "--style=file", str(f)])
        if formatted_proc.returncode != 0:
            # Don't abort the whole run for one failed file; surface it in the log.
            diff_parts.append(f"=== {rel(f)} (clang-format failed rc={formatted_proc.returncode}) ===\n"
                              f"{formatted_proc.stderr}\n")
            bad.append((1, rel(f)))
            continue
        formatted = formatted_proc.stdout.splitlines(keepends=True)
        if original == formatted:
            continue
        udiff = list(difflib.unified_diff(
            original, formatted,
            fromfile=f"a/{rel(f)}",
            tofile=f"b/{rel(f)}",
        ))
        if not udiff:
            continue
        changed = sum(1 for ln in udiff
                      if ln.startswith(("+", "-")) and not ln.startswith(("+++", "---")))
        bad.append((changed, rel(f)))
        diff_parts.append(f"=== {rel(f)} ===\n")
        diff_parts.extend(udiff)
        diff_parts.append("\n")

    diff_file.write_text("".join(diff_parts))
    bad.sort(reverse=True)
    per_file_path.write_text("".join(f"{n:6d}  {p}\n" for n, p in bad))

    summary = f"{len(bad)} / {len(files)} file(s) need formatting"
    if bad:
        warn(summary)
        warn(f"diff: {rel(diff_file)}")
        warn("fix:  qa-tools/refactoring/code-format/apply-format.py")
        return CheckResult(
            name="clang-format", status="issues",
            summary=summary,
            counts={"needs_format": len(bad), "total": len(files)},
            log_path=diff_file,
            duration_s=time.time() - started,
        )
    ok(summary)
    return CheckResult(
        name="clang-format", status="pass",
        summary=f"0 / {len(files)} file(s) need formatting",
        counts={"needs_format": 0, "total": len(files)},
        duration_s=time.time() - started,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="*", help="Optional scope (repo-relative).")
    args = ap.parse_args()
    paths = args.paths or scope_paths_from_env()
    res = check_format(paths)
    return res.exit_code


if __name__ == "__main__":
    sys.exit(main())
