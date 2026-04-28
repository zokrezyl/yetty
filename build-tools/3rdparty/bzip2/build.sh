#!/usr/bin/env bash
# bzip2 3rdparty wrapper.
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|macos-x86_64|macos-arm64|\
    android-arm64-v8a|android-x86_64|ios-arm64|ios-x86_64|tvos-arm64|webasm)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}" ;;
    *) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac
[ "${USE_NIX:-1}" = "0" ] && exec bash "$(dirname "$0")/_build.sh" "$@"
cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/bzip2/_build.sh "$@"
