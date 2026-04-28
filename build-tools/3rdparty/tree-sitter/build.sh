#!/usr/bin/env bash
# tree-sitter 3rdparty wrapper: picks the right nix shell for $TARGET_PLATFORM
# and re-execs into _build.sh inside it.
#
# Required env:
#   TARGET_PLATFORM   one of:
#     linux-x86_64 | linux-aarch64 |
#     macos-arm64 | macos-x86_64 |
#     android-arm64-v8a | android-x86_64 |
#     ios-arm64 | ios-x86_64 | tvos-arm64|tvos-x86_64 |
#     webasm
#   OUTPUT_DIR        where the tarball is written
#
# Version (core tree-sitter) is read from ./version. Grammar tags are
# pinned inside _build.sh.

set -euo pipefail

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|\
    macos-x86_64|macos-arm64|\
    android-arm64-v8a|android-x86_64|\
    ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64|\
    webasm)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}"
        ;;
    windows-x86_64)
        echo "tree-sitter windows-x86_64 not implemented yet (defer like other libs)" >&2
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
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/tree-sitter/_build.sh "$@"
