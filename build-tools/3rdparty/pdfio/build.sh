#!/usr/bin/env bash
# pdfio 3rdparty wrapper.

set -euo pipefail

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|\
    macos-x86_64|macos-arm64|\
    android-arm64-v8a|android-x86_64|\
    ios-arm64|ios-x86_64|tvos-arm64|\
    webasm)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}"
        ;;
    # windows-x86_64 intentionally absent — yetty.exe is being switched
    # to native MSVC on the windows-libs-msvc branch; an MSYS2 CLANG64
    # build here would produce ABI-incompatible libs. TODO: add the
    # MSVC path once windows-libs-msvc lands on main.
    *)
        echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
        exit 1
        ;;
esac

if [ "${USE_NIX:-1}" = "0" ]; then
    exec bash "$(dirname "$0")/_build.sh" "$@"
fi

cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/pdfio/_build.sh "$@"
