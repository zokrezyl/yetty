#!/usr/bin/env bash
# glfw3webgpu 3rdparty wrapper — desktop only.
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|macos-x86_64|macos-arm64)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}" ;;
    windows-x86_64)
        if ! command -v cl >/dev/null 2>&1 && ! command -v cl.exe >/dev/null 2>&1; then
            echo "error: windows-x86_64 requires MSVC cl on PATH (vcvarsall x64)" >&2
            exit 1
        fi
        exec bash "$(dirname "$0")/_build.sh" "$@" ;;
    *) echo "glfw3webgpu is desktop-only (linux + macos + windows) — unsupported TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac
[ "${USE_NIX:-1}" = "0" ] && exec bash "$(dirname "$0")/_build.sh" "$@"
cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/glfw3webgpu/_build.sh "$@"
