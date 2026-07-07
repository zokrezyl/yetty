#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""ci-package.py — bundle the build outputs the analysis check jobs need.

On GitHub Actions every job runs on its own runner, so the build-dependent
checkers (LibTooling tools, clang-tidy) cannot see the build job's tree.
Instead of re-running the full desktop build per check job, the build job
packs the minimal subset the checkers actually consume:

  - compile_commands.json,
  - the LibTooling checker binaries built under <build>/qa-tools/,
  - the header-like files inside every build-dir include path referenced
    by the compile database (generated headers, fetched 3rdparty headers).

Everything else the checkers read (the sources, in-tree headers) comes from
the repository checkout, and system headers come from the same apt packages
the build job installs. Include dirs are discovered from the compile
database itself, so new -I paths added by the build are picked up without
touching this script. Only header-like files are packed: the referenced
dirs also hold huge binary payloads (embedded installer assets) that AST
parsing never opens.

The archive unpacks relative to the repo root, recreating the build-dir
paths compile_commands.json refers to (runner workspace paths are identical
across jobs of one workflow).

Usage:
  ci-package.py [--build-dir DIR] [--output TAR_GZ]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tarfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

HEADER_EXTENSIONS = (".h", ".hpp", ".hh", ".hxx", ".inc", ".inl", ".def")

INCLUDE_FLAG_PATTERN = re.compile(r"-(?:I ?|isystem ?)(\S+)")


def referenced_include_dirs(database_path: Path, build_dir: Path) -> list[Path]:
    entries = json.loads(database_path.read_text())
    include_dirs = set()
    for entry in entries:
        command = entry.get("command") or " ".join(entry.get("arguments", []))
        for flag_path in INCLUDE_FLAG_PATTERN.findall(command):
            candidate = Path(flag_path)
            if not candidate.is_absolute():
                candidate = Path(entry["directory"]) / candidate
            candidate = Path(os.path.realpath(candidate))
            if candidate.is_dir() and candidate.is_relative_to(build_dir):
                include_dirs.add(candidate)
    return sorted(include_dirs)


def header_files(include_dirs: list[Path]) -> list[Path]:
    collected = set()
    for include_dir in include_dirs:
        for current_dir, subdir_names, file_names in os.walk(include_dir):
            for file_name in file_names:
                if file_name.endswith(HEADER_EXTENSIONS):
                    collected.add(Path(current_dir) / file_name)
    return sorted(collected)


def checker_binaries(build_dir: Path) -> list[Path]:
    binaries = []
    qa_tools_build = build_dir / "qa-tools"
    for current_dir, subdir_names, file_names in os.walk(qa_tools_build):
        if "CMakeFiles" in subdir_names:
            subdir_names.remove("CMakeFiles")
        for file_name in file_names:
            path = Path(current_dir) / file_name
            if path.is_file() and os.access(path, os.X_OK):
                binaries.append(path)
    return sorted(binaries)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build-desktop-ytrace-release")
    parser.add_argument("--output", default="tmp/qa/ci/qa-build.tar.gz")
    arguments = parser.parse_args()

    build_dir = (REPO_ROOT / arguments.build_dir).resolve()
    database_path = build_dir / "compile_commands.json"
    if not database_path.is_file():
        print(f"error: {database_path} not found — run the build first")
        return 2

    include_dirs = referenced_include_dirs(database_path, build_dir)
    headers = header_files(include_dirs)
    binaries = checker_binaries(build_dir)
    if not binaries:
        print(f"error: no checker binaries under {build_dir}/qa-tools — "
              "was the build configured with YETTY_ENABLE_TOOL_QA=ON?")
        return 2

    output_path = REPO_ROOT / arguments.output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    members = [database_path, *headers, *binaries]
    with tarfile.open(output_path, "w:gz") as archive:
        for member in members:
            archive.add(member, arcname=str(member.relative_to(REPO_ROOT)))

    size_mb = output_path.stat().st_size / (1024 * 1024)
    print(f"packed {len(headers)} headers from {len(include_dirs)} include "
          f"dirs, {len(binaries)} checker binaries, compile_commands.json")
    print(f"-> {output_path.relative_to(REPO_ROOT)} ({size_mb:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
