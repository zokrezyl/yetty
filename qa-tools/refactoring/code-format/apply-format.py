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
import queue
import sys
import threading
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
    # runs, so worker threads give real parallelism. One worker per core.
    workers = min(total, os.cpu_count() or 1) or 1

    # Producer/consumer with two queues. The main thread fills `work` with every
    # file plus one None sentinel per worker (so each worker is guaranteed a
    # stop signal and none can hang waiting on an empty queue), then becomes the
    # SOLE writer to stdout, draining exactly `total` completions off `results`.
    # Workers only run clang-format and report the finished file back — no
    # shared stdout, so progress lines can never interleave.
    work: "queue.Queue" = queue.Queue()
    results: "queue.Queue" = queue.Queue()
    for f in files:
        work.put(f)
    for _ in range(workers):
        work.put(None)

    def worker() -> None:
        while True:
            f = work.get()
            if f is None:  # sentinel: no more files for this worker
                return
            run([binary, "--style=file", "-i", str(f)])
            results.put(f)

    threads = [threading.Thread(target=worker, name=f"fmt-{i}", daemon=True)
               for i in range(workers)]
    for t in threads:
        t.start()

    # One progress line per completed file, in completion order. The loop ends
    # after exactly `total` results, so it can never block past the last file.
    for done in range(1, total + 1):
        f = results.get()
        try:
            display = Path(f).relative_to(REPO_ROOT)
        except ValueError:
            display = f
        progress = f"[{done}/{total}] {display}"
        if is_tty:
            # Overwrite the same line; \033[K clears to EOL so a shorter path
            # doesn't leave tail characters from a longer previous one.
            sys.stdout.write(f"\r\033[K{progress}")
            sys.stdout.flush()
        else:
            print(progress)

    for t in threads:
        t.join()

    if is_tty and total:
        sys.stdout.write("\r\033[K")  # drop the progress line before the summary
        sys.stdout.flush()
    ok(f"reformatted {total} file(s) in place with {workers} worker(s)")

    if (REPO_ROOT / ".git").exists():
        proc = run(["git", "-C", str(REPO_ROOT), "diff", "--name-only"])
        changed = sum(1 for _ in proc.stdout.splitlines())
        info(f"git sees {changed} modified file(s) — review with 'git diff'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
