#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""ci-step.py — run one static-analysis check as a CI pipeline step.

Runs the named check, echoes its output into the step log, and records a
machine-readable result under tmp/qa/ci/<check>.json for the final report
step (ci-report.py) to aggregate.

Always exits 0 for a check that ran, whatever it found: the tag-build
analysis pipeline tracks findings over time, it does not gate on them.
Only an unknown check name is a usage error (exit 2).

Usage:
  ci-step.py <check-name>
  ci-step.py --list
"""

from __future__ import annotations

import importlib.util
import json
import re
import subprocess
import sys
import time
import traceback
from pathlib import Path

QA_TOOLS_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = QA_TOOLS_DIR.parent
CI_RESULT_DIR = REPO_ROOT / "tmp" / "qa" / "ci"

sys.path.insert(0, str(QA_TOOLS_DIR))
from _common import scope_paths_from_env  # noqa: E402

# Python-wrapper checks living in qa-tools/analysis/:
# check name -> (script, entry function, whether it takes QA_PATHS scope).
PYTHON_CHECKS = {
    "format":      ("analysis/check-format.py",      "check_format",      True),
    "clang-tidy":  ("analysis/check-clang-tidy.py",  "check_clang_tidy",  True),
    "cppcheck":    ("analysis/check-cppcheck.py",    "check_cppcheck",    True),
    "scan-build":  ("analysis/check-scan-build.py",  "check_scan_build",  False),
    "osv-scanner": ("analysis/check-osv-scanner.py", "check_osv_scanner", False),
    "trivy":       ("analysis/check-trivy.py",       "check_trivy",       False),
    "grype":       ("analysis/check-grype.py",       "check_grype",       False),
}

# LibTooling checks built by the desktop build (YETTY_ENABLE_TOOL_QA):
# check name -> (runner script, regex for the closing violation-count line).
# xargs may split the source list across several checker invocations, each
# printing its own summary line, so the counts of ALL matches are summed.
LIBTOOLING_CHECKS = {
    "result-checker": ("analysis/result-checker/run-result-checker.sh",
                       r"Total violations: (\d+)"),
    "out-param": ("analysis/out-param/run-out-param-checker.sh",
                  r"Decomposed-value out-parameter violations: (\d+)"),
    "naming-convention": ("analysis/naming-convention/run-naming-convention.sh",
                          r"Total violations: (\d+)"),
}

# Step logs are the only durable record of a CI run (the workspace is
# discarded), but a checker can emit tens of thousands of lines. Echo at
# most this many, then skip to the tail so the closing summary stays
# visible.
MAX_ECHOED_LINES = 2000
ECHOED_TAIL_LINES = 10


def load_check_function(script_rel_path: str, attr: str):
    """Load a hyphenated script under qa-tools/ as a module, return an attr."""
    script_path = QA_TOOLS_DIR / script_rel_path
    spec = importlib.util.spec_from_file_location(
        script_path.stem.replace("-", "_"), script_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return getattr(module, attr)


def primary_count(counts: dict, summary: str, status: str) -> int | None:
    """Best single 'how many findings' number for the trend line.

    Finding-specific keys come first: check-format's "total" is the number
    of files SCANNED, its finding count is "needs_format".
    """
    for key in ("needs_format", "bugs", "total"):
        if key in counts:
            return counts[key]
    leading_number = re.match(r"(\d+)\b", summary)
    if leading_number:
        return int(leading_number.group(1))
    return 0 if status == "pass" else None


def run_python_check(name: str) -> dict:
    script_rel_path, attr, takes_scope = PYTHON_CHECKS[name]
    check_function = load_check_function(script_rel_path, attr)
    result = check_function(scope_paths_from_env()) if takes_scope \
        else check_function()
    return {
        "name": name,
        "status": result.status,
        "count": primary_count(result.counts, result.summary, result.status),
        "summary": result.summary,
        "counts": result.counts,
        "duration_s": round(result.duration_s, 1),
    }


def echo_log(log_path: Path) -> None:
    lines = log_path.read_text(errors="replace").splitlines()
    for line in lines[:MAX_ECHOED_LINES]:
        print(line)
    if len(lines) > MAX_ECHOED_LINES:
        skipped = len(lines) - MAX_ECHOED_LINES - ECHOED_TAIL_LINES
        if skipped > 0:
            print(f"[... {skipped} lines omitted, full log: "
                  f"{log_path.relative_to(REPO_ROOT)} ...]")
        for line in lines[-ECHOED_TAIL_LINES:]:
            print(line)


def run_libtooling_check(name: str) -> dict:
    runner_rel_path, count_pattern = LIBTOOLING_CHECKS[name]
    runner = QA_TOOLS_DIR / runner_rel_path
    log_path = CI_RESULT_DIR / f"{name}.log"
    started = time.time()
    with log_path.open("w") as log_file:
        completed = subprocess.run([str(runner)], stdout=log_file,
                                   stderr=subprocess.STDOUT, cwd=REPO_ROOT)
    echo_log(log_path)

    output = log_path.read_text(errors="replace")
    found = [int(number) for number in re.findall(count_pattern, output)]
    if completed.returncode == 0:
        status, count, summary = "pass", 0, "no violations"
    elif found:
        count = sum(found)
        status = "issues" if count else "pass"
        summary = f"{count} violation(s)"
    else:
        # Non-zero exit without a summary line: the checker never ran
        # (missing binary, missing compile_commands.json, ...).
        status, count = "fail", None
        summary = f"checker did not run (exit {completed.returncode})"
    return {
        "name": name,
        "status": status,
        "count": count,
        "summary": summary,
        "counts": {"total": count} if count is not None else {},
        "duration_s": round(time.time() - started, 1),
    }


def main() -> int:
    known_checks = list(LIBTOOLING_CHECKS) + list(PYTHON_CHECKS)
    if len(sys.argv) != 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        print("known checks:", " ".join(known_checks))
        return 2
    if sys.argv[1] == "--list":
        print("\n".join(known_checks))
        return 0

    name = sys.argv[1]
    if name not in known_checks:
        print(f"error: unknown check '{name}'; one of: "
              + " ".join(known_checks))
        return 2

    CI_RESULT_DIR.mkdir(parents=True, exist_ok=True)
    try:
        if name in LIBTOOLING_CHECKS:
            result = run_libtooling_check(name)
        else:
            result = run_python_check(name)
    except Exception as exception:
        traceback.print_exc()
        result = {
            "name": name,
            "status": "fail",
            "count": None,
            "summary": f"crashed: {exception}",
            "counts": {},
            "duration_s": 0.0,
        }

    result_path = CI_RESULT_DIR / f"{name}.json"
    result_path.write_text(json.dumps(result, indent=2) + "\n")
    print(f"\n[{name}] {result['status']}  count={result['count']}  "
          f"({result['duration_s']}s)  {result['summary']}")
    print(f"[{name}] result recorded: {result_path.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
