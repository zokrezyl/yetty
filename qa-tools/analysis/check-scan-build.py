#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""check-scan-build.py — run Clang Static Analyzer via scan-build."""

from __future__ import annotations

import argparse
import os
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from _common import (  # noqa: E402
    REPO_ROOT,
    TMP_DIR,
    CheckResult,
    err,
    info,
    ok,
    pick_tool,
    rel,
    run,
    warn,
)

SCAN_BUILD_CANDIDATES = ("scan-build", "scan-build-21", "scan-build-18",
                         "scan-build-17", "scan-build-15", "scan-build-14",
                         "scan-build-9")


def check_scan_build() -> CheckResult:
    started = time.time()
    binary = os.environ.get("SCAN_BUILD") or pick_tool(*SCAN_BUILD_CANDIDATES)
    if not binary:
        err("scan-build not installed (apt install clang-tools-<ver>)")
        return CheckResult(name="scan-build", status="fail",
                           summary="scan-build not installed",
                           duration_s=time.time() - started)
    if not pick_tool("cmake"):
        err("cmake is required for scan-build")
        return CheckResult(name="scan-build", status="fail",
                           summary="cmake not installed",
                           duration_s=time.time() - started)

    src_dir = Path(os.environ.get("QA_SB_SRC_DIR", REPO_ROOT))
    build_dir = Path(os.environ.get("QA_SB_BUILD_DIR", TMP_DIR / "scan-build-build"))
    report_dir = TMP_DIR / "scan-build-report"
    log_path = TMP_DIR / "scan-build.log"
    build_dir.mkdir(parents=True, exist_ok=True)
    report_dir.mkdir(parents=True, exist_ok=True)
    log_path.write_text("")

    info(f"using {binary}")
    info(f"source:  {rel(src_dir)}")
    info(f"build:   {rel(build_dir)}")
    info(f"reports: {rel(report_dir)}")

    with log_path.open("w") as lf:
        cfg = run([binary, "-o", str(report_dir),
                   "cmake", str(src_dir), "-DCMAKE_BUILD_TYPE=Debug"],
                  cwd=build_dir)
        lf.write(cfg.stdout or ""); lf.write(cfg.stderr or "")
        if cfg.returncode != 0:
            err("cmake configure under scan-build failed")
            err(f"see {rel(log_path)}")
            return CheckResult(name="scan-build", status="fail",
                               summary="cmake configure failed",
                               log_path=log_path,
                               duration_s=time.time() - started)

        jobs = str(os.cpu_count() or 4)
        build = run([binary, "-o", str(report_dir),
                     "--status-bugs", "--keep-empty",
                     "cmake", "--build", ".", "--", f"-j{jobs}"],
                    cwd=build_dir)
        lf.write(build.stdout or ""); lf.write(build.stderr or "")

    # Find latest per-run report subdirectory.
    run_dirs = sorted((p for p in report_dir.glob("*/") if p.is_dir()),
                      key=lambda p: p.stat().st_mtime, reverse=True)
    if not run_dirs:
        ok("scan-build produced no report (no bugs found)")
        return CheckResult(name="scan-build", status="pass",
                           summary="no bugs", duration_s=time.time() - started)
    latest = run_dirs[0]
    index = latest / "index.html"
    bugs = 0
    if index.exists():
        bugs = len(re.findall(r'<tr class="bt_', index.read_text(errors="replace")))
    if bugs == 0:
        ok("scan-build found no issues")
        return CheckResult(name="scan-build", status="pass",
                           summary="no bugs",
                           duration_s=time.time() - started)
    warn(f"{bugs} potential bug(s)")
    warn(f"open {rel(index)}")
    return CheckResult(name="scan-build", status="issues",
                       summary=f"{bugs} potential bug(s)",
                       details=[f"report: {rel(index)}"],
                       counts={"bugs": bugs}, log_path=log_path,
                       duration_s=time.time() - started)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.parse_args()
    return check_scan_build().exit_code


if __name__ == "__main__":
    sys.exit(main())
