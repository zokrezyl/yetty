#!/usr/bin/env python3
"""yclass codegen per-module runner.

There is NO module list here. A yclass module registers itself in its own
CMakeLists.txt via `yetty_yclass_module(<name> [COMPAT_HEADER]
[DEFINES <macro>...])` (see build-tools/yetty/yclass-codegen.cmake); the
build graph invokes this script once per registered module with the
module's identity on the command line.

The build graph (target `yclass-codegen`) and humans both go through this
script; the Makefile's `codegen` target delegates to that ninja target,
which re-runs a module only when its annotated sources (or the
generator) changed.

Modes:
  --run MODULE --source-dir DIR --repo-root DIR --compile-db FILE
        Discover MODULE's annotated sources under DIR and run codegen.py
        for it (pass 1 of the two-pass scheme).
  --run-if-headers-changed MODULE --since STAMP --source-dir DIR \\
        --repo-root DIR --compile-db FILE
        Pass 2: re-run codegen for MODULE only when some generated header
        (include/yetty/api/**, src/yetty/gen/impl/**/*.h) is newer than
        STAMP — i.e. another module's pass 1 actually changed a
        cross-module-visible type. codegen skips content-identical writes,
        so header mtimes move only on real changes and an unchanged tree
        short-circuits here.

Optional per-module configuration (mirrors the CMake registration):
  --defines "MACRO MACRO..."   feature-guard macros for the clang parse
  --compat-header 0|1          also emit the legacy-path header

Single-module regeneration by hand:
  python3 src/yetty/yclass/gen/driver.py --run yscene \\
      --source-dir src/yetty/yscene \\
      --repo-root . --compile-db build-desktop-ytrace-release/compile_commands.json
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


def annotated_sources(repo_root: Path, module_name: str, source_dir: str) -> list:
    """Every non-generated .c under the module dir that declares a class or
    mixin of this module — as repo-root-RELATIVE paths, because the paths
    land verbatim in the committed model.yaml (`source_file:`) and must not
    carry a machine-specific prefix. Byte-order sorted (LC_ALL=C sort)."""
    pattern = re.compile(
        r'(clang::annotate|YETTY_ANNOTATE)\("(class|mixin)@'
        + re.escape(module_name) + r':')
    matches = []
    for path in (repo_root / source_dir).rglob("*.c"):
        if path.name.endswith(".gen.c"):
            continue
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        if pattern.search(text):
            matches.append(path.relative_to(repo_root))
    return sorted(matches, key=lambda path: str(path))


def run_codegen(repo_root: Path, compile_db: Path, module_name: str, source_dir: str,
                defines: str, compat_header: bool) -> None:
    sources = annotated_sources(repo_root, module_name, source_dir)
    if not sources:
        sys.exit(f"driver: no annotated sources under {source_dir}")
    generator = repo_root / "src" / "yetty" / "yclass" / "gen" / "codegen.py"
    environment = dict(
        os.environ,
        PYTHONHASHSEED="0",
        YCLASS_COMPILE_DB=str(compile_db),
        YCLASS_DEFINES=defines,
        YCLASS_COMPAT_HEADER="1" if compat_header else "",
    )
    argv = [sys.executable, str(generator), module_name,
            str(repo_root / "include" / "yetty"),
            str(repo_root / source_dir)] + [str(s) for s in sources]
    completed = subprocess.run(argv, cwd=str(repo_root), env=environment)
    if completed.returncode != 0:
        sys.exit(completed.returncode)


def generated_headers_newer_than(repo_root: Path, stamp: Path) -> bool:
    if not stamp.exists():
        return True
    stamp_mtime = stamp.stat().st_mtime
    header_roots = [repo_root / "include" / "yetty" / "api",
                    repo_root / "src" / "yetty" / "gen" / "impl"]
    for root in header_roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*.h"):
            if path.stat().st_mtime > stamp_mtime:
                return True
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", metavar="MODULE")
    parser.add_argument("--run-if-headers-changed", metavar="MODULE")
    parser.add_argument("--since", metavar="STAMP")
    parser.add_argument("--source-dir", metavar="DIR")
    parser.add_argument("--defines", metavar="MACROS", default="")
    parser.add_argument("--compat-header", metavar="0|1", default="0")
    parser.add_argument("--repo-root", metavar="DIR")
    parser.add_argument("--compile-db", metavar="FILE")
    arguments = parser.parse_args()

    if not arguments.repo_root or not arguments.compile_db or not arguments.source_dir:
        sys.exit("driver: --repo-root, --compile-db and --source-dir are required")
    repo_root = Path(arguments.repo_root).resolve()
    compile_db = Path(arguments.compile_db).resolve()
    compat_header = arguments.compat_header.strip() == "1"

    if arguments.run:
        run_codegen(repo_root, compile_db, arguments.run, arguments.source_dir,
                    arguments.defines, compat_header)
        return

    if arguments.run_if_headers_changed:
        if not arguments.since:
            sys.exit("driver: --run-if-headers-changed requires --since")
        if generated_headers_newer_than(repo_root, Path(arguments.since)):
            run_codegen(repo_root, compile_db, arguments.run_if_headers_changed,
                        arguments.source_dir, arguments.defines, compat_header)
        return

    parser.print_help()
    sys.exit(2)


if __name__ == "__main__":
    main()
