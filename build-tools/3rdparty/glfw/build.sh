#!/usr/bin/env bash
# glfw 3rdparty wrapper — desktop only (linux + macos + windows).
# Caller is expected to have the platform's stock toolchain on PATH:
# gcc + make + cmake + ninja + pkg-config (apt-installed on linux);
# Xcode CommandLineTools + brew cmake/ninja on macOS; MSVC via
# vcvarsall on Windows.

set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|linux-riscv64|macos-x86_64|macos-arm64)
        ;;
    windows-x86_64)
        if ! command -v cl >/dev/null 2>&1 && ! command -v cl.exe >/dev/null 2>&1; then
            echo "error: windows-x86_64 requires MSVC cl on PATH (vcvarsall x64)" >&2
            exit 1
        fi
        ;;
    *)
        echo "glfw is desktop-only (linux + macos + windows) — unsupported TARGET_PLATFORM: $TARGET_PLATFORM" >&2
        exit 1 ;;
esac

exec bash "$(dirname "$0")/_build.sh" "$@"
