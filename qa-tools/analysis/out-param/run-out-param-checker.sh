#!/bin/bash
# Run out-param-checker on all compiled yetty C files.
#
# The check flags functions that decompose a small value into a cluster of
# same-typed scalar pointer out-parameters (two `int *`, three `float *`, ...)
# instead of returning it by value. Offending functions declared in headers are
# reported at their header declaration (and deduped), reached transitively
# through the .c translation units that include them.
#
# Extra arguments are forwarded to the checker:
#   ./run-out-param-checker.sh                       # default check
#   ./run-out-param-checker.sh --min-count=3         # only clusters of 3+
#   ./run-out-param-checker.sh --verbose

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
YETTY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${YETTY_ROOT}/build-desktop-ytrace-release"
CHECKER="${BUILD_DIR}/qa-tools/analysis/out-param/out-param-checker"

if [ ! -x "$CHECKER" ]; then
    echo "Error: out-param-checker not found at $CHECKER"
    echo "Build it first: make build-desktop-ytrace-release"
    exit 1
fi

if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
    echo "Error: compile_commands.json not found"
    exit 1
fi

# Only feed the checker files that actually have an entry in the compilation
# database. The src/yetty tree carries many .c files that are never compiled in
# a desktop-linux build (windows/webasm/android/macos sources, plus *.gen.c
# units that are #included into a parent rather than compiled standalone). With
# no compile_commands.json entry, LibTooling falls back to a flagless guess and
# drowns the real diagnostics in "fatal error: '<x>.h' file not found" noise.
# Intersect the source list with the database so each file is analysed with its
# recorded compile flags, and skipped silently when it has none.
mapfile -t source_files < <(
    python3 - "${BUILD_DIR}/compile_commands.json" "${YETTY_ROOT}/src/yetty" <<'PYEOF'
import json, os, sys

database_path, source_root = sys.argv[1], sys.argv[2]

with open(database_path) as database_file:
    compiled = {os.path.realpath(entry["file"]) for entry in json.load(database_file)}

for current_dir, _subdirs, names in os.walk(source_root):
    for name in names:
        if not name.endswith(".c"):
            continue
        path = os.path.join(current_dir, name)
        if os.path.realpath(path) in compiled:
            print(path)
PYEOF
)

if [ "${#source_files[@]}" -eq 0 ]; then
    echo "Error: no source files matched compile_commands.json entries"
    exit 1
fi

printf '%s\n' "${source_files[@]}" | \
    xargs "$CHECKER" -p "$BUILD_DIR" "$@" 2>&1
