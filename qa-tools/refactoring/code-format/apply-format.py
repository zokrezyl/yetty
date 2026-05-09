#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""apply-format.py — run clang-format -i on our C/H files.

Refuses to run on a dirty working tree unless QA_FORMAT_FORCE=1.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
from _common import (  # noqa: E402
    REPO_ROOT,
    err,
    info,
    list_sources,
    ok,
    pick_tool,
    run,
    scope_paths_from_env,
    warn,
)

CLANG_FORMAT_CANDIDATES = (
    "clang-format-21",
    "clang-format-20",
    "clang-format-19",
    "clang-format-18",
    "clang-format-17",
    "clang-format-15",
    "clang-format-14",
    "clang-format-9",
)


def tree_is_dirty() -> bool:
    if not (REPO_ROOT / ".git").exists():
        return False
    proc = run(["git", "-C", str(REPO_ROOT), "diff", "--quiet", "--exit-code"])
    return proc.returncode != 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="*", help="Optional scope (repo-relative).")
    ap.add_argument("--force", action="store_true",
                    help="Proceed even if the working tree is dirty.")
    args = ap.parse_args()

    binary = os.environ.get("CLANG_FORMAT") or pick_tool(*CLANG_FORMAT_CANDIDATES)
    if not binary:
        err("no clang-format binary found")
        return 2

    if not args.force and not os.environ.get("QA_FORMAT_FORCE") and tree_is_dirty():
        warn("working tree has unstaged changes")
        warn("commit/stash first, or pass --force (or QA_FORMAT_FORCE=1)")
        return 3

    version = run([binary, "--version"]).stdout.strip().splitlines()[0]
    info(f"using {binary} ({version})")

    paths = args.paths or scope_paths_from_env()
    files = list_sources(paths)
    for f in files:
        run([binary, "--style=file", "-i", str(f)])
    ok(f"reformatted {len(files)} file(s) in place")

    if (REPO_ROOT / ".git").exists():
        proc = run(["git", "-C", str(REPO_ROOT), "diff", "--name-only"])
        changed = sum(1 for _ in proc.stdout.splitlines())
        info(f"git sees {changed} modified file(s) — review with 'git diff'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
