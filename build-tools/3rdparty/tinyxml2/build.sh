#!/usr/bin/env bash
# tinyxml2 3rdparty wrapper. 9-target matrix (no windows-x86_64 — same
# MSVC ABI concern as libco/pdfio/thorvg/libssh2; TODO: add MSVC path
# once windows-libs-msvc lands on main).
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 |
#                     webasm
#   OUTPUT_DIR        where the tarball is written

set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|\
    macos-x86_64|macos-arm64|\
    android-arm64-v8a|android-x86_64|\
    ios-arm64|ios-x86_64|tvos-arm64|\
    webasm)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}" ;;
    *) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

if [ "${USE_NIX:-1}" = "0" ]; then
    exec bash "$(dirname "$0")/_build.sh" "$@"
fi
cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/tinyxml2/_build.sh "$@"
