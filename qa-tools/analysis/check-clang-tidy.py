#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""check-clang-tidy.py — run clang-tidy across our C sources.

Requires a compile_commands.json. Auto-discovers one under build-*/.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from _common import (  # noqa: E402
    REPO_ROOT,
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

CLANG_TIDY_CANDIDATES = (
    "clang-tidy", 
    "clang-tidy-21", 
    "clang-tidy-20", 
    "clang-tidy-19", 
    "clang-tidy-18", 
    "clang-tidy-17",
    "clang-tidy-15", "clang-tidy-14",
)
DIAG_RE = re.compile(r"^(?P<file>[^:]+):(?P<line>\d+):\d+: (?P<sev>warning|error): (?P<msg>.*?) \[(?P<rule>[^\]]+)\]$")


def locate_compile_db() -> Path | None:
    env_dir = os.environ.get("QA_BUILD_DIR")
    if env_dir:
        p = REPO_ROOT / env_dir / "compile_commands.json"
        return p.parent if p.is_file() else None
    root_db = REPO_ROOT / "compile_commands.json"
    if root_db.is_file():
        return REPO_ROOT
    for build_dir in sorted(REPO_ROOT.glob("build-*")):
        if (build_dir / "compile_commands.json").is_file():
            return build_dir
    return None


def check_clang_tidy(paths: list[str] | None = None) -> CheckResult:
    started = time.time()
    binary = os.environ.get("CLANG_TIDY") or pick_tool(*CLANG_TIDY_CANDIDATES)
    if not binary:
        err("no clang-tidy binary found")
        return CheckResult(name="clang-tidy", status="fail",
                           summary="clang-tidy not installed",
                           duration_s=time.time() - started)

    db_dir = locate_compile_db()
    if not db_dir:
        err("no compile_commands.json found")
        err("run 'make build-desktop-ytrace-release' (or similar) first,")
        err("or set QA_BUILD_DIR=<dir>")
        return CheckResult(name="clang-tidy", status="fail",
                           summary="no compile_commands.json",
                           details=["hint: 'make build-desktop-ytrace-release'"],
                           duration_s=time.time() - started)

    checks = os.environ.get(
        "QA_TIDY_CHECKS",
        "bugprone-*,clang-analyzer-*,performance-*,portability-*,"
        "readability-braces-around-statements,misc-*,"
        "-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling",
    )

    files = list_sources(paths)
    if not files:
        warn("no source files found")
        return CheckResult(name="clang-tidy", status="pass",
                           summary="no files", duration_s=time.time() - started)

    log_path = TMP_DIR / "clang-tidy.log"
    log_path.write_text("")
    log_fh = log_path.open("w")

    info(f"using {binary}")
    info(f"compile db: {rel(db_dir)}")
    info(f"checks: {checks}")

    rule_counts: Counter = Counter()
    per_file: Counter = Counter()
    errors: list[str] = []
    total = 0

    for f in files:
        proc = run([binary, "-p", str(db_dir), f"--checks={checks}", "--quiet", str(f)])
        out = (proc.stdout or "") + (proc.stderr or "")
        if not out.strip():
            continue
        log_fh.write(f"=== {rel(f)} ===\n{out}\n")
        for ln in out.splitlines():
            m = DIAG_RE.match(ln)
            if not m:
                continue
            total += 1
            rule_counts[m.group("rule")] += 1
            per_file[rel(m.group("file"))] += 1
            if m.group("sev") == "error":
                errors.append(f"{rel(m.group('file'))}:{m.group('line')}  "
                              f"{m.group('msg')} [{m.group('rule')}]")
    log_fh.close()

    (TMP_DIR / "clang-tidy.top-rules.txt").write_text(
        "\n".join(f"{n:6d}  {r}" for r, n in rule_counts.most_common()) + "\n"
    )
    (TMP_DIR / "clang-tidy.summary.txt").write_text(
        "\n".join(f"{n:6d}  {f}" for f, n in per_file.most_common()) + "\n"
    )

    details = []
    if rule_counts:
        details.append("top checks: "
                       + ", ".join(f"{r}({n})" for r, n in rule_counts.most_common(5)))
    if errors:
        details.append(f"{len(errors)} error(s) — see clang-tidy.log")

    summary = f"{total} diagnostic(s)" if total else "no diagnostics"
    if total:
        warn(summary)
        status = "issues"
    else:
        ok(summary)
        status = "pass"

    return CheckResult(
        name="clang-tidy", status=status, summary=summary, details=details,
        counts={"total": total}, log_path=log_path,
        duration_s=time.time() - started,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="*")
    args = ap.parse_args()
    paths = args.paths or scope_paths_from_env()
    return check_clang_tidy(paths).exit_code


if __name__ == "__main__":
    sys.exit(main())
