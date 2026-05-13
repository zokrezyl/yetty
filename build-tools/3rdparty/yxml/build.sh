#!/usr/bin/env bash
# yxml 3rdparty wrapper. yxml (Yorhel/yxml) is a tiny streaming XML parser
# — a single .c + .h pair, MIT-licensed. Upstream doesn't ship release
# tarballs and doesn't tag releases: we fetch the .c/.h directly from
# cgit's `/plain/?id=<commit>` endpoint, then compile to a static
# libyxml.a per target platform. Matches the structural pattern of
# tinyxml2/libcss/lz4 — _build.sh does the actual compile inside a
# per-platform nix dev shell.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | linux-riscv64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 |
#                     tvos-arm64 | tvos-x86_64 |
#                     webasm | windows-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Optional:
#   USE_NIX=0         skip the `nix develop` wrapper (use whatever
#                     compilers/PATH the caller already has — useful
#                     when the GH workflow installs deps via apt).

set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|linux-riscv64|\
    macos-x86_64|macos-arm64|\
    android-arm64-v8a|android-x86_64|\
    ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64|\
    webasm)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}" ;;
    windows-x86_64)
        # Native MSVC under vcvarsall x64. There's no nix shell for
        # windows-MSVC; the caller (the workflow / a developer) sets up
        # cl.exe + lib.exe on PATH and runs _build.sh directly.
        if ! command -v cl >/dev/null 2>&1 && ! command -v cl.exe >/dev/null 2>&1; then
            echo "error: windows-x86_64 requires MSVC cl on PATH (vcvarsall x64)" >&2
            exit 1
        fi
        exec bash "$(dirname "$0")/_build.sh" "$@" ;;
    *) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

if [ "${USE_NIX:-1}" = "0" ]; then
    exec bash "$(dirname "$0")/_build.sh" "$@"
fi
cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/yxml/_build.sh "$@"
