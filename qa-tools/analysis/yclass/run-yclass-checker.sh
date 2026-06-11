#!/bin/bash
# Run yclass-checker on every compiled .c file that includes yclass/class.h.
#
# Extra arguments are forwarded to the checker:
#   ./run-yclass-checker.sh                              # default check
#   ./run-yclass-checker.sh --verbose
#   ./run-yclass-checker.sh --check-object-arith=false   # disable the check

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
YETTY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${YETTY_ROOT}/build-desktop-ytrace-release"
CHECKER="${BUILD_DIR}/qa-tools/analysis/yclass/yclass-checker"

if [ ! -x "$CHECKER" ]; then
    echo "Error: yclass-checker not found at $CHECKER"
    echo "Build it first: make build-desktop-ytrace-release"
    exit 1
fi

if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
    echo "Error: compile_commands.json not found"
    exit 1
fi

# Scope: files that use the yclass object model, i.e. those that include
# <yetty/yclass/class.h>. Restrict to that set (the check is meaningless
# elsewhere) AND intersect with the compilation database — a file with no
# compile_commands.json entry (windows/webasm/android sources, plus *.gen.c
# units that are #included into a parent rather than compiled standalone)
# would otherwise be analysed with a flagless guess and drown the real
# diagnostics in "fatal error: '<x>.h' file not found" noise.
mapfile -t source_files < <(
    python3 - "${BUILD_DIR}/compile_commands.json" "${YETTY_ROOT}/src" "${YETTY_ROOT}/tools" <<'PYEOF'
import json, os, re, sys

database_path = sys.argv[1]
roots = sys.argv[2:]

with open(database_path) as database_file:
    compiled = {os.path.realpath(entry["file"]) for entry in json.load(database_file)}

include_re = re.compile(r'#\s*include\s*[<"]yetty/yclass/class\.h[>"]')

for root in roots:
    for current_dir, _subdirs, names in os.walk(root):
        for name in names:
            if not name.endswith(".c"):
                continue
            path = os.path.join(current_dir, name)
            if os.path.realpath(path) not in compiled:
                continue
            try:
                with open(path, encoding="utf-8", errors="replace") as source:
                    if include_re.search(source.read()):
                        print(path)
            except OSError:
                pass
PYEOF
)

if [ "${#source_files[@]}" -eq 0 ]; then
    echo "Error: no class.h-including source files matched compile_commands.json entries"
    exit 1
fi

printf '%s\n' "${source_files[@]}" | \
    xargs "$CHECKER" -p "$BUILD_DIR" "$@" 2>&1
