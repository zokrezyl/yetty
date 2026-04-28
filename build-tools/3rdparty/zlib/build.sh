#!/usr/bin/env bash
# zlib 3rdparty wrapper. Uses upstream madler/zlib (the original)
# rather than zlib-ng — same ABI, simpler cross-compile, fewer
# per-target surprises. yetty consumers reference ZLIB::ZLIB which we
# expose either way.
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
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/zlib/_build.sh "$@"
