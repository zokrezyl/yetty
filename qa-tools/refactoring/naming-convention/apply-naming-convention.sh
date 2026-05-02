#!/bin/bash
# Driver for naming-convention-fix.
#
# Runs the refactoring binary across all yetty C files, writes per-file
# YAML Replacement files, and (optionally) applies them in-place via
# clang-apply-replacements-18.
#
# Usage:
#   apply-naming-convention.sh                  # dry-run, prints plan
#   apply-naming-convention.sh --write          # writes YAML to tmp/naming-fixes/
#   apply-naming-convention.sh --apply          # writes YAML and applies
#   apply-naming-convention.sh --only-kind=struct-bound --apply
#
# Auto-fix scope: Rules 1, 2, 3 (decl + use sites). Rule 4 (typedefs)
# is reported by the analysis tool, never auto-fixed (human review).

set -u

# Note: no `set -e`. The fixer binary returns non-zero whenever any TU
# emits warnings/errors during clang parsing, but it still writes valid
# YAML for all successfully-parsed TUs. We tolerate that and continue.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
YETTY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${YETTY_ROOT}/build-desktop-ytrace-release"
FIXER="${BUILD_DIR}/qa-tools/refactoring/naming-convention/naming-convention-fix"
OUT_DIR="${YETTY_ROOT}/tmp/naming-fixes"
APPLY_TOOL="$(command -v clang-apply-replacements-18 || command -v clang-apply-replacements || true)"

mode="dry"
extra_args=()

for arg in "$@"; do
    case "$arg" in
        --write) mode="write" ;;
        --apply) mode="apply" ;;
        *)       extra_args+=("$arg") ;;
    esac
done

if [ ! -x "$FIXER" ]; then
    echo "Error: naming-convention-fix not found at $FIXER" >&2
    echo "Build it first: make build-desktop-ytrace-release" >&2
    exit 1
fi
if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
    echo "Error: compile_commands.json not found in $BUILD_DIR" >&2
    exit 1
fi

if [ "$mode" = "dry" ]; then
    find "${YETTY_ROOT}/src/yetty" -name "*.c" -type f | \
        xargs "$FIXER" -p "$BUILD_DIR" "${extra_args[@]}"
    exit
fi

# write or apply: clear output dir, run with --out
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

find "${YETTY_ROOT}/src" "${YETTY_ROOT}/tools" "${YETTY_ROOT}/demo" "${YETTY_ROOT}/test" \
    -type f \
    \( -name "*.c" -o -name "*.cpp" -o -name "*.cc" -o -name "*.mm" -o -name "*.m" \) \
    ! -path "*/src/libvterm*" ! -path "*/src/tinyemu*" ! -path "*/qa-tools/*" | \
    xargs "$FIXER" -p "$BUILD_DIR" --out="$OUT_DIR" "${extra_args[@]}"

if [ "$mode" = "write" ]; then
    echo "YAML written to $OUT_DIR"
    echo "Apply manually: $APPLY_TOOL $OUT_DIR"
    exit
fi

# apply
if [ -z "$APPLY_TOOL" ]; then
    echo "Error: clang-apply-replacements not in PATH; install clang-tools" >&2
    exit 1
fi
echo "Applying replacements via $APPLY_TOOL ..."
"$APPLY_TOOL" "$OUT_DIR"
echo "Done. Re-run the analysis tool to verify remaining violations."
