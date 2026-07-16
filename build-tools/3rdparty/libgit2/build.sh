#!/usr/bin/env bash
# libgit2 3rdparty wrapper. Runs the from-source recipe (_build.sh) inside
# the matching nix dev shell so the toolchain (cmake, ninja, cc) is the same
# one CI uses. Set USE_NIX=0 to run _build.sh directly against the host
# toolchain (what local desktop builds do when the nix shell is unavailable).
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|linux-riscv64|\
    macos-x86_64|macos-arm64)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM%-mt}" ;;
    windows-x86_64)
        # Native MSVC: caller must have vcvarsall'd the shell so cl.exe is
        # on PATH. No nix shell for the MSVC ABI build.
        if ! command -v cl >/dev/null 2>&1 && ! command -v cl.exe >/dev/null 2>&1; then
            echo "error: windows-x86_64 requires MSVC cl on PATH (vcvarsall x64)" >&2
            exit 1
        fi
        exec bash "$(dirname "$0")/_build.sh" "$@"
        ;;
    *) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

if [ "${USE_NIX:-1}" = "0" ]; then
    exec bash "$(dirname "$0")/_build.sh" "$@"
fi
cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/libgit2/_build.sh "$@"
