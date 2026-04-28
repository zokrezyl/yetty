#!/usr/bin/env bash
# libco 3rdparty wrapper. Note: emscripten is intentionally absent from
# the platform list — yetty's webasm coroutine backend uses
# emscripten_fiber_t (Asyncify) directly, see ycoroutine.c on that
# platform. Same exclusion the from-source libs/co.cmake had.
#
# windows-x86_64 is also intentionally absent: the rest of yetty.exe is
# being switched to native MSVC (see the windows-libs-msvc branch). The
# MSVC pattern hasn't landed on main yet, so adding an MSYS2 CLANG64
# build here would produce ABI-incompatible libs that can't link with
# the MSVC yetty.exe. TODO: add windows-x86_64 once windows-libs-msvc
# lands and we can mirror libuv's MSVC path.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64
#   OUTPUT_DIR        where the tarball is written

set -euo pipefail

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|\
    macos-x86_64|macos-arm64|\
    android-arm64-v8a|android-x86_64|\
    ios-arm64|ios-x86_64|tvos-arm64)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}"
        ;;
    webasm)
        echo "libco does not target webasm — yetty's webasm coroutines use" >&2
        echo "emscripten_fiber_t (Asyncify) instead. Skipping." >&2
        exit 1
        ;;
    *)
        echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
        exit 1
        ;;
esac

if [ "${USE_NIX:-1}" = "0" ]; then
    exec bash "$(dirname "$0")/_build.sh" "$@"
fi

cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/libco/_build.sh "$@"
