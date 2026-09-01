#!/usr/bin/env bash
# libcss 3rdparty wrapper. NetSurf's MIT CSS cascade + its two MIT deps
# (libparserutils + libwapcaplet), built from the VENDORED tree at
# src/libcss — no upstream download; the repo checkout is the source.
#
# Real builds run on linux-* / macos-* through the generic
# 3rdparty-<target> shells (they carry cmake-via-PATH, ninja, perl and
# the per-target compiler — everything the standalone CMakeLists
# needs). Every other target writes a placeholder UNSUPPORTED tarball
# in _build.sh's case block, so it needs no toolchain shell at all.
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|linux-riscv64|\
    macos-x86_64|macos-arm64)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}"
        ;;
    android-arm64-v8a|android-x86_64|\
    ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64|\
    webasm|windows-x86_64)
        # No nix shell — _build.sh's case block writes a placeholder
        # tarball directly. Skip the nix-develop wrapping entirely.
        exec bash "$(dirname "$0")/_build.sh" "$@"
        ;;
    *) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

if [ "${USE_NIX:-1}" = "0" ]; then
    exec bash "$(dirname "$0")/_build.sh" "$@"
fi
cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/libcss/_build.sh "$@"
