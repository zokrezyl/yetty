#!/usr/bin/env bash
# QEMU asset wrapper: picks the right nix shell for $TARGET_PLATFORM and
# re-execs into _build.sh inside it.
#
# Required env:
#   TARGET_PLATFORM   one of:
#     linux-x86_64 | linux-aarch64 | windows-x86_64 |
#     android-arm64-v8a | android-x86_64 |
#     macos-arm64 | macos-x86_64 | ios-arm64 | ios-x86_64 |
#     tvos-arm64 | tvos-x86_64
#   VERSION, OUTPUT_DIR  see _build.sh
#
# Note: no wasm target — the webasm yetty build uses in-process TinyEMU
# (compiled to wasm), not a prebuilt QEMU binary.

set -euo pipefail

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|android-arm64-v8a|android-x86_64|\
    macos-x86_64|macos-arm64|ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64)
        SHELL_NAME="assets-qemu-${TARGET_PLATFORM}"
        ;;
    webasm)
        echo "qemu does not target webasm — the webasm yetty build uses in-process" >&2
        echo "TinyEMU (compiled to wasm) instead of a prebuilt QEMU binary. Skipping." >&2
        exit 1
        ;;
    windows-x86_64)
        # Windows uses MSYS2 CLANG64 — no nix shell. The caller must already
        # be inside CLANG64 (CI: msys2/setup-msys2 with msystem: CLANG64;
        # locally: launch from the CLANG64 start menu shortcut, or run from
        # any MSYS2 shell with `MSYSTEM=CLANG64 bash -lc ...`). Required
        # packages are listed in _build.sh's windows-x86_64 case.
        if [ "${MSYSTEM:-}" != "CLANG64" ]; then
            echo "error: windows-x86_64 must run inside MSYS2 CLANG64 (MSYSTEM=${MSYSTEM:-unset})" >&2
            echo "       launch a CLANG64 shell, or in CI use msys2/setup-msys2 with msystem: CLANG64" >&2
            exit 1
        fi
        exec bash "$(dirname "$0")/_build.sh" "$@"
        ;;
    *)
        echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
        exit 1
        ;;
esac

cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/qemu/_build.sh "$@"
