#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""apply-clang-tidy.py — run clang-tidy --fix on our C sources.

Default check is readability-braces-around-statements (a safe,
high-volume, mechanical fix). Override with --checks or QA_TIDY_CHECKS.

Refuses on a dirty working tree unless --force / QA_FORMAT_FORCE=1 —
applied fixes are easier to audit when 'git diff' shows only the rewrite.

Uses run-clang-tidy(-N) for parallel + header-safe rewrites
(via clang-apply-replacements). Falls back to a serial loop if
run-clang-tidy is missing.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
from _common import (  # noqa: E402
    DEFAULT_SOURCE_ROOTS,
    REPO_ROOT,
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
RUN_CLANG_TIDY_CANDIDATES = (
    "run-clang-tidy", "run-clang-tidy-21", "run-clang-tidy-18",
    "run-clang-tidy-17", "run-clang-tidy-15", "run-clang-tidy-14",
)
DEFAULT_CHECKS = "readability-braces-around-statements"


def locate_compile_db() -> Path | None:
    env_dir = os.environ.get("QA_BUILD_DIR")
    if env_dir:
        p = REPO_ROOT / env_dir / "compile_commands.json"
        return p.parent if p.is_file() else None
    if (REPO_ROOT / "compile_commands.json").is_file():
        return REPO_ROOT
    for build_dir in sorted(REPO_ROOT.glob("build-*")):
        if (build_dir / "compile_commands.json").is_file():
            return build_dir
    return None


def tree_is_dirty() -> bool:
    if not (REPO_ROOT / ".git").exists():
        return False
    proc = run(["git", "-C", str(REPO_ROOT), "diff", "--quiet", "--exit-code"])
    return proc.returncode != 0


def header_filter(scope: list[str] | None) -> str:
    return "(" + "|".join(scope or list(DEFAULT_SOURCE_ROOTS)) + ")"


def apply_parallel(driver: str, tidy: str, db_dir: Path,
                   checks: str, scope: list[str] | None) -> int:
    jobs = str(os.cpu_count() or 4)
    cmd = [
        driver,
        "-clang-tidy-binary", tidy,
        "-p", str(db_dir),
        f"-checks=-*,{checks}",
        f"-header-filter={header_filter(scope)}",
        "-fix",
        "-quiet",
        "-j", jobs,
    ]
    cmd.extend(scope or list(DEFAULT_SOURCE_ROOTS))
    info(f"using {driver} (-j {jobs})")
    info("scope: " + " ".join(scope or list(DEFAULT_SOURCE_ROOTS)))
    return run(cmd, capture=False).returncode


def apply_serial(tidy: str, db_dir: Path, checks: str,
                 scope: list[str] | None) -> int:
    files = [f for f in list_sources(scope) if f.suffix == ".c"]
    if not files:
        warn("no .c files in scope")
        return 0
    info(f"using {tidy} (sequential — {len(files)} TU(s))")
    rc = 0
    for f in files:
        proc = run([
            tidy, "-p", str(db_dir),
            f"--checks=-*,{checks}",
            f"--header-filter={header_filter(scope)}",
            "--fix", "--fix-errors", "--quiet", str(f),
        ])
        if proc.returncode != 0:
            rc = proc.returncode
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="*", help="Optional scope (repo-relative).")
    ap.add_argument("--checks",
                    default=os.environ.get("QA_TIDY_CHECKS", DEFAULT_CHECKS),
                    help=f"clang-tidy check selector (default: {DEFAULT_CHECKS}).")
    ap.add_argument("--force", action="store_true",
                    help="Proceed even if the working tree is dirty.")
    args = ap.parse_args()

    tidy = os.environ.get("CLANG_TIDY") or pick_tool(*CLANG_TIDY_CANDIDATES)
    if not tidy:
        err("no clang-tidy binary found")
        return 2

    db_dir = locate_compile_db()
    if not db_dir:
        err("no compile_commands.json found")
        err("build first ('make build-desktop-ytrace-release') or set QA_BUILD_DIR")
        return 2

    if not args.force and not os.environ.get("QA_FORMAT_FORCE") and tree_is_dirty():
        warn("working tree has unstaged changes")
        warn("commit/stash first, or pass --force (or QA_FORMAT_FORCE=1)")
        return 3

    scope = args.paths or scope_paths_from_env()
    info(f"compile db: {rel(db_dir)}")
    info(f"checks: {args.checks}")

    driver = os.environ.get("RUN_CLANG_TIDY") or pick_tool(*RUN_CLANG_TIDY_CANDIDATES)
    if driver:
        rc = apply_parallel(driver, tidy, db_dir, args.checks, scope)
    else:
        warn("run-clang-tidy not found — falling back to serial loop")
        rc = apply_serial(tidy, db_dir, args.checks, scope)

    if rc != 0:
        warn(f"clang-tidy returned rc={rc} — fixes may be partial")
    ok("clang-tidy --fix completed")

    if (REPO_ROOT / ".git").exists():
        proc = run(["git", "-C", str(REPO_ROOT), "diff", "--name-only"])
        changed = sum(1 for _ in proc.stdout.splitlines())
        info(f"git sees {changed} modified file(s) — review with 'git diff'")
        if changed:
            info("hint: run apply-format.py to normalize whitespace afterward")
    return 0


if __name__ == "__main__":
    sys.exit(main())
