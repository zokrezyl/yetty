#!/usr/bin/env bash
# Builds 3rdparty libs for $TARGET_PLATFORM whose tarballs aren't yet
# in the cmake fetch cache (~/.cache/yetty/3rdparty/ by default). The
# resulting tarballs land directly in that cache, so a subsequent yetty
# cmake configure picks them up without going to GitHub releases.
#
# Use this when you've added/edited a producer locally and want to
# iterate on yetty's main build without round-tripping through CI.
#
# Usage:
#   TARGET_PLATFORM=linux-x86_64 ./build-uncached.sh
#   TARGET_PLATFORM=android-x86_64 ./build-uncached.sh
#   TARGET_PLATFORM=linux-x86_64 ./build-uncached.sh libpng freetype
#
# With no positional args: builds every lib whose tarball is missing.
# With positional args: only that subset.
#
# Per-target wrappers (build-uncached-<target>.sh) just set
# TARGET_PLATFORM and exec us.
#
# Behaviour:
#   - .noarch libs: always one tarball, only built on a linux host —
#     non-linux invocations skip them (they're already covered).
#   - Desktop-only libs whose build.sh rejects this $TARGET_PLATFORM:
#     marked SKIPPED and we move on (no abort).

set -euo pipefail

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required (linux-x86_64 / android-x86_64 / ...)}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CACHE_DIR="${YETTY_3RDPARTY_CACHE_DIR:-$HOME/.cache/yetty/3rdparty}"

mkdir -p "$CACHE_DIR"

# Optional whitelist of libs (positional args). Empty = all.
declare -a ONLY=("$@")
in_whitelist() {
    local lib="$1"
    [ "${#ONLY[@]}" -eq 0 ] && return 0
    for _w in "${ONLY[@]}"; do [ "$_w" = "$lib" ] && return 0; done
    return 1
}

declare -a TODO=()
declare -a SKIP_CACHED=()
declare -a SKIP_NOARCH_NONLINUX=()

# Discover candidate libs (subdirs with a `version` file).
for _dir in "$SCRIPT_DIR"/*/; do
    [ -d "$_dir" ] || continue
    _lib="$(basename "$_dir")"
    [ -f "$_dir/version" ] || continue
    in_whitelist "$_lib" || continue

    _ver="$(tr -d '[:space:]' < "$_dir/version")"
    if [ -f "$_dir/.noarch" ]; then
        _tar="$CACHE_DIR/$_lib-$_ver.tar.gz"
        # noarch is host-independent — build it on linux only to avoid
        # rebuilding the same artifact for every TARGET_PLATFORM. (You
        # can force-rebuild by deleting the cache file.)
        if [ "$TARGET_PLATFORM" != "linux-x86_64" ]; then
            SKIP_NOARCH_NONLINUX+=("$_lib")
            continue
        fi
    else
        _tar="$CACHE_DIR/$_lib-$TARGET_PLATFORM-$_ver.tar.gz"
    fi

    if [ -f "$_tar" ]; then
        SKIP_CACHED+=("$_lib")
        continue
    fi
    TODO+=("$_lib")
done

if [ "${#SKIP_CACHED[@]}" -gt 0 ]; then
    echo "==> already cached (skipping): ${SKIP_CACHED[*]}"
fi
if [ "${#SKIP_NOARCH_NONLINUX[@]}" -gt 0 ]; then
    echo "==> noarch on non-linux host (skipping; build via build-uncached-linux.sh): ${SKIP_NOARCH_NONLINUX[*]}"
fi
if [ "${#TODO[@]}" -eq 0 ]; then
    echo "nothing to build for $TARGET_PLATFORM — cache is complete."
    exit 0
fi

#-----------------------------------------------------------------------------
# Order TODO by build-time dep tier — producers that fetch other 3rdparty
# tarballs at build time need their deps cached first. Anything not
# explicitly listed is tier 0 (no inter-3rdparty build-time deps).
#
# Tier 1 (depends on tier 0):
#   libpng   — fetches prebuilt zlib at build time
#   freetype — fetches prebuilt zlib at build time
#   libssh2  — fetches prebuilt openssl at build time
#   libcurl  — fetches prebuilt openssl at build time
#   glfw     — system X11 only (still build before glfw3webgpu)
# Tier 2 (depends on tier 1):
#   msdfgen      — fetches prebuilt freetype + tinyxml2
#   glfw3webgpu  — fetches prebuilt glfw
#-----------------------------------------------------------------------------
_tier() {
    case "$1" in
        msdfgen|glfw3webgpu)                 echo 2 ;;
        libpng|freetype|libssh2|libcurl|glfw) echo 1 ;;
        *)                                   echo 0 ;;
    esac
}
declare -a SORTED_TODO=()
for _tier_n in 0 1 2; do
    for _l in "${TODO[@]}"; do
        if [ "$(_tier "$_l")" = "$_tier_n" ]; then
            SORTED_TODO+=("$_l")
        fi
    done
done
TODO=("${SORTED_TODO[@]}")

echo "==> will build for $TARGET_PLATFORM: ${TODO[*]}"
echo "==> output dir: $CACHE_DIR"
echo

#-----------------------------------------------------------------------------
# Run each producer, tolerating "this lib doesn't target $TARGET_PLATFORM"
# rejections from build.sh (e.g. glfw on android, libco on webasm).
#-----------------------------------------------------------------------------
declare -a OK=()
declare -a SKIPPED=()
declare -a FAILED=()

for _lib in "${TODO[@]}"; do
    echo "----------------------------------------------------------------------"
    echo "==> building $_lib for $TARGET_PLATFORM"
    echo "----------------------------------------------------------------------"

    # Run the producer; capture stdout+stderr to a per-lib log file we
    # can inspect on failure (instead of dumping the whole transcript
    # to the terminal — keeps things readable when many libs build).
    _log="$REPO_ROOT/tmp/3rdparty-${_lib}-${TARGET_PLATFORM}.log"
    mkdir -p "$(dirname "$_log")"

    # Share CACHE_DIR (producer's source/dep download cache) with the
    # cmake fetch cache so dep-fetching producers (libssh2 needing
    # openssl, libpng/freetype needing zlib, msdfgen needing
    # freetype+tinyxml2, glfw3webgpu needing glfw) can resolve deps from
    # locally-built tarballs instead of always going to GitHub releases.
    set +e
    TARGET_PLATFORM="$TARGET_PLATFORM" \
    OUTPUT_DIR="$CACHE_DIR" \
    CACHE_DIR="$CACHE_DIR" \
    "$SCRIPT_DIR/$_lib/build.sh" >"$_log" 2>&1
    _rc=$?
    set -e

    # Heuristic: if build.sh exited because $TARGET_PLATFORM isn't in its
    # supported list, the message contains "unsupported" or
    # "desktop-only" or "does not target". Treat that as SKIPPED.
    if [ "$_rc" -ne 0 ] && grep -qiE 'unsupported TARGET_PLATFORM|desktop-only|does not target|use emscripten_fiber_t' "$_log"; then
        echo "    SKIPPED ($_lib doesn't target $TARGET_PLATFORM — see $_log)"
        SKIPPED+=("$_lib")
        continue
    fi
    if [ "$_rc" -ne 0 ]; then
        echo "    FAILED ($_rc) — see $_log"
        FAILED+=("$_lib")
        continue
    fi

    # Verify the tarball actually landed.
    if [ -f "$_dir/.noarch" ] || [ -f "$SCRIPT_DIR/$_lib/.noarch" ]; then
        _expect_glob="$CACHE_DIR/$_lib-*.tar.gz"
    else
        _expect_glob="$CACHE_DIR/$_lib-$TARGET_PLATFORM-*.tar.gz"
    fi
    if ! ls $_expect_glob >/dev/null 2>&1; then
        echo "    FAILED — build returned 0 but no tarball at $_expect_glob (see $_log)"
        FAILED+=("$_lib")
        continue
    fi

    echo "    OK"
    OK+=("$_lib")
done

echo
echo "======================================================================"
echo "$TARGET_PLATFORM summary:"
echo "  OK      (${#OK[@]}):      ${OK[*]:-<none>}"
echo "  SKIPPED (${#SKIPPED[@]}): ${SKIPPED[*]:-<none>}"
echo "  FAILED  (${#FAILED[@]}):  ${FAILED[*]:-<none>}"
echo "======================================================================"

[ "${#FAILED[@]}" -eq 0 ]
