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
import threading
from concurrent.futures import ThreadPoolExecutor
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
    total = len(files)
    is_tty = sys.stdout.isatty()

    # clang-format is an external subprocess that releases the GIL while it
    # runs, so a thread pool gives real parallelism. One worker per core.
    workers = min(total, os.cpu_count() or 1) or 1

    # stdout is shared across workers: serialize every write under one lock and
    # bump the shared progress counter inside the same critical section so the
    # [done/total] count stays monotonic and lines never interleave.
    print_lock = threading.Lock()
    done = 0

    def format_one(f) -> None:
        nonlocal done
        run([binary, "--style=file", "-i", str(f)])
        try:
            display = Path(f).relative_to(REPO_ROOT)
        except ValueError:
            display = f
        with print_lock:
            done += 1
            progress = f"[{done}/{total}] {display}"
            if is_tty:
                # Overwrite the same line; \033[K clears to EOL so a shorter
                # path doesn't leave tail characters from a longer previous one.
                sys.stdout.write(f"\r\033[K{progress}")
                sys.stdout.flush()
            else:
                print(progress)

    with ThreadPoolExecutor(max_workers=workers) as pool:
        # Drain the iterator so any worker exception propagates here.
        for _ in pool.map(format_one, files):
            pass

    if is_tty and total:
        sys.stdout.write("\r\033[K")  # drop the progress line before the summary
        sys.stdout.flush()
    ok(f"reformatted {len(files)} file(s) in place with {workers} worker(s)")

    if (REPO_ROOT / ".git").exists():
        proc = run(["git", "-C", str(REPO_ROOT), "diff", "--name-only"])
        changed = sum(1 for _ in proc.stdout.splitlines())
        info(f"git sees {changed} modified file(s) — review with 'git diff'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
