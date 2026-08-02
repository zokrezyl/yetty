#!/usr/bin/env python3
"""yclass codegen driver — the ONE module table plus the per-module runner.

The build graph (build-tools/yetty/yclass-codegen.cmake, target
`yclass-codegen`) and humans both go through this script; the Makefile's
`codegen` target delegates to that ninja target, which re-runs a module
only when its annotated sources (or the generator) changed.

Modes:
  --list-modules
        Print one `name|source_dir` line per module (consumed by CMake at
        configure time).
  --run MODULE --repo-root DIR --compile-db FILE
        Discover MODULE's annotated sources and run codegen.py for it
        (pass 1 of the two-pass scheme).
  --run-if-headers-changed MODULE --since STAMP --repo-root DIR --compile-db FILE
        Pass 2: re-run codegen for MODULE only when some generated header
        (include/yetty/api/**, src/yetty/gen/impl/**/*.h) is newer than
        STAMP — i.e. another module's pass 1 actually changed a
        cross-module-visible type. codegen skips content-identical writes,
        so header mtimes move only on real changes and an unchanged tree
        short-circuits here.

Single-module regeneration by hand:
  python3 src/yetty/yclass/gen/driver.py --run ygrid \
      --repo-root . --compile-db build-desktop-ytrace-release/compile_commands.json
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# The module table. `source_dir` defaults to src/yetty/<name>.
#   defines        — feature-guard macros the codegen clang parse needs so
#                    annotations behind #ifdef are visible (keep in sync
#                    with the owning tool's CMake defines).
#   compat_header  — ALSO emit the legacy-path header for modules whose
#                    old include/yetty/<module>/ headers still have
#                    un-flipped consumers. Retire per module as consumers
#                    move to include/yetty/api/.
MODULES = [
    {"name": "yapp"},
    {"name": "yetty"},
    {"name": "yfigure"},
    {"name": "ygrid"},
    {"name": "ygit"},
    {"name": "ygui", "compat_header": True},
    {"name": "yguiapp", "compat_header": True},
    {"name": "ymgui"},
    {"name": "yrdawn"},
    {"name": "yshadertoy"},
    {"name": "yvterm"},
    {"name": "yscene"},
    {"name": "yflame"},
    {"name": "ymap"},
    {"name": "ynotebook"},
    {"name": "yjupyter"},
    {"name": "yview"},
    {"name": "yplatform", "compat_header": True},
    {"name": "ychrome"},
    {"name": "ymusic"},
    {"name": "ycircuit"},
    {"name": "yai", "source_dir": "tools/ai/yai"},
    {"name": "yrich", "compat_header": True},
    {"name": "yzoo", "source_dir": "tools/yzoo"},
    {"name": "ymaze", "source_dir": "tools/ymaze"},
    {"name": "yjungle", "source_dir": "tools/yjungle"},
    {"name": "demoygui", "source_dir": "demo/ygui"},
    {"name": "ycompositor", "source_dir": "tools/ycompositor"},
    {"name": "yaudio", "source_dir": "tools/yaudio"},
    {"name": "ycompositorygui", "source_dir": "tools/ycompositor-ygui"},
    {"name": "ybrowser", "source_dir": "tools/ybrowser",
     "defines": ["YETTY_YBROWSER_HAS_STANDALONE", "YETTY_YGUI_HAS_UV"]},
    {"name": "yhello", "source_dir": "tools/yhello",
     "defines": ["YETTY_YHELLO_HAS_STANDALONE"]},
    {"name": "ygreeter", "source_dir": "tools/ygreeter",
     "defines": ["YETTY_YGREETER_HAS_STANDALONE", "YETTY_YGUI_HAS_UV"]},
    {"name": "ynet"},
    {"name": "api_yplot", "source_dir": "src/api/yplot"},
    {"name": "ydummy"},
    {"name": "ytermsink"},
    {"name": "yterminal"},
]

def module_entry(module_name: str) -> dict:
    for entry in MODULES:
        if entry["name"] == module_name:
            return entry
    sys.exit(f"driver: unknown module '{module_name}'")


def module_source_dir(entry: dict) -> str:
    return entry.get("source_dir", f"src/yetty/{entry['name']}")


def annotated_sources(repo_root: Path, entry: dict) -> list:
    """Every non-generated .c under the module dir that declares a class or
    mixin of this module — as repo-root-RELATIVE paths, because the paths
    land verbatim in the committed model.yaml (`source_file:`) and must not
    carry a machine-specific prefix. Byte-order sorted (LC_ALL=C sort)."""
    pattern = re.compile(
        r'(clang::annotate|YETTY_ANNOTATE)\("(class|mixin)@'
        + re.escape(entry["name"]) + r':')
    source_dir = repo_root / module_source_dir(entry)
    matches = []
    for path in source_dir.rglob("*.c"):
        if path.name.endswith(".gen.c"):
            continue
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        if pattern.search(text):
            matches.append(path.relative_to(repo_root))
    return sorted(matches, key=lambda path: str(path))


def run_codegen(repo_root: Path, compile_db: Path, entry: dict) -> None:
    sources = annotated_sources(repo_root, entry)
    if not sources:
        sys.exit(f"driver: no annotated sources under {module_source_dir(entry)}")
    generator = repo_root / "src" / "yetty" / "yclass" / "gen" / "codegen.py"
    environment = dict(
        os.environ,
        PYTHONHASHSEED="0",
        YCLASS_COMPILE_DB=str(compile_db),
        YCLASS_DEFINES=" ".join(entry.get("defines", [])),
        YCLASS_COMPAT_HEADER="1" if entry.get("compat_header") else "",
    )
    argv = [sys.executable, str(generator), entry["name"],
            str(repo_root / "include" / "yetty"),
            str(repo_root / module_source_dir(entry))] + [str(s) for s in sources]
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
    parser.add_argument("--list-modules", action="store_true")
    parser.add_argument("--run", metavar="MODULE")
    parser.add_argument("--run-if-headers-changed", metavar="MODULE")
    parser.add_argument("--since", metavar="STAMP")
    parser.add_argument("--repo-root", metavar="DIR")
    parser.add_argument("--compile-db", metavar="FILE")
    arguments = parser.parse_args()

    if arguments.list_modules:
        for entry in MODULES:
            print(f"{entry['name']}|{module_source_dir(entry)}")
        return

    if not arguments.repo_root or not arguments.compile_db:
        sys.exit("driver: --repo-root and --compile-db are required")
    repo_root = Path(arguments.repo_root).resolve()
    compile_db = Path(arguments.compile_db).resolve()

    if arguments.run:
        run_codegen(repo_root, compile_db, module_entry(arguments.run))
        return

    if arguments.run_if_headers_changed:
        if not arguments.since:
            sys.exit("driver: --run-if-headers-changed requires --since")
        entry = module_entry(arguments.run_if_headers_changed)
        if generated_headers_newer_than(repo_root, Path(arguments.since)):
            run_codegen(repo_root, compile_db, entry)
        return

    parser.print_help()
    sys.exit(2)


if __name__ == "__main__":
    main()
