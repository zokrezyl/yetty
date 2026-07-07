#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""ci-report.py — aggregate per-check CI results into the tag-build QA report.

Reads the tmp/qa/ci/<check>.json files written by ci-step.py and prints one
summary table plus one stable `QA-TREND` line per check. The QA-TREND lines
are the time series: grep the report step log of successive tag builds for
"QA-TREND" to follow how the counts evolve.

The report never fails the pipeline — checks that could not run are listed
loudly ("unavailable") but only humans act on that.
"""

from __future__ import annotations

import datetime
import json
import os
import sys
from pathlib import Path

QA_TOOLS_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = QA_TOOLS_DIR.parent
CI_RESULT_DIR = REPO_ROOT / "tmp" / "qa" / "ci"

# Display order. Any extra <check>.json found in the result directory is
# appended, so a new pipeline step shows up even before it is listed here.
KNOWN_CHECKS = (
    "result-checker",
    "out-param",
    "naming-convention",
    "format",
    "clang-tidy",
    "cppcheck",
    "scan-build",
    "osv-scanner",
    "trivy",
    "grype",
)

RULE = "=" * 78
THIN_RULE = "-" * 78


def load_results() -> list[dict]:
    results = []
    seen = set()
    for name in KNOWN_CHECKS:
        result_path = CI_RESULT_DIR / f"{name}.json"
        if result_path.exists():
            results.append(json.loads(result_path.read_text()))
        else:
            results.append({"name": name, "status": "missing", "count": None,
                            "summary": "no result recorded (step crashed "
                                       "or was skipped)"})
        seen.add(name)
    for extra_path in sorted(CI_RESULT_DIR.glob("*.json")):
        if extra_path.stem not in seen:
            results.append(json.loads(extra_path.read_text()))
    return results


def main() -> int:
    tag = os.environ.get("CI_COMMIT_TAG", "")
    commit = os.environ.get("CI_COMMIT_SHA", "")[:10]
    pipeline = os.environ.get("CI_PIPELINE_NUMBER", "")
    today = datetime.date.today().isoformat()

    results = load_results()

    report_lines = []
    report_lines.append(RULE)
    report_lines.append(f" STATIC ANALYSIS REPORT  tag={tag or '-'}  "
                        f"commit={commit or '-'}  pipeline={pipeline or '-'}  "
                        f"{today}")
    report_lines.append(RULE)
    report_lines.append(f" {'check':<18} {'status':<8} {'count':>7}  summary")
    report_lines.append(THIN_RULE)

    total_findings = 0
    checks_with_findings = 0
    unavailable = []
    for result in results:
        count = result.get("count")
        count_text = "-" if count is None else str(count)
        report_lines.append(f" {result['name']:<18} {result['status']:<8} "
                            f"{count_text:>7}  {result.get('summary', '')}")
        if isinstance(count, int):
            total_findings += count
            if count > 0:
                checks_with_findings += 1
        if result["status"] in ("fail", "missing"):
            unavailable.append(result["name"])

    report_lines.append(THIN_RULE)
    report_lines.append(f" open findings: {total_findings} across "
                        f"{checks_with_findings} check(s)")
    if unavailable:
        report_lines.append(f" UNAVAILABLE (did not run, no data point): "
                            f"{' '.join(unavailable)}")
    report_lines.append("")

    # Stable machine-readable series, one line per check. Each line is
    # self-contained so a single grep hit carries its full context.
    context = f"pipeline={pipeline or '-'} tag={tag or '-'} " \
              f"commit={commit or '-'} date={today}"
    for result in results:
        count = result.get("count")
        count_text = "NA" if count is None else str(count)
        report_lines.append(f"QA-TREND {context} check={result['name']} "
                            f"status={result['status']} count={count_text}")
    report_lines.append(f"QA-TREND {context} check=TOTAL status=- "
                        f"count={total_findings}")
    report_lines.append(RULE)

    report_text = "\n".join(report_lines) + "\n"
    print(report_text, end="")

    CI_RESULT_DIR.mkdir(parents=True, exist_ok=True)
    report_path = CI_RESULT_DIR / "report.txt"
    report_path.write_text(report_text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
