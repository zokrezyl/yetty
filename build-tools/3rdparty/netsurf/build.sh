#!/usr/bin/env bash
# netsurf-all 3rdparty wrapper. NetSurf 3.11 (the source-full release —
# core browser + helper libs: libcss, libdom, libhubbub, libwapcaplet,
# libparserutils, libnsutils, libnsbmp, libnsgif, libnslog, libnspsl,
# libsvgtiny, libutf8proc).
#
# Shell mapping (different from lexbor — netsurf needs gperf/bison/flex
# host tools + expat/libxml2/jpeg/png/webp target headers, which the
# generic 3rdparty-<platform> shells don't carry):
#
#   linux-x86_64       → 3rdparty-netsurf
#   linux-aarch64      → 3rdparty-netsurf-linux-aarch64
#   linux-riscv64      → 3rdparty-netsurf-linux-riscv64
#   macos-arm64        → 3rdparty-netsurf-macos-arm64
#   macos-x86_64       → 3rdparty-netsurf-macos-x86_64
#   android-*, ios-*,  → no nix shell — _build.sh's per-platform branch
#   tvos-*, webasm,      writes a placeholder UNSUPPORTED tarball and
#   windows-x86_64       exits 0; the consumer-side netsurf.cmake
#                        detects the marker and skips silently.
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64)
        SHELL_NAME="3rdparty-netsurf"
        ;;
    macos-arm64|macos-x86_64)
        SHELL_NAME="3rdparty-netsurf-${TARGET_PLATFORM}"
        ;;
    linux-aarch64|linux-riscv64|\
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
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/netsurf/_build.sh "$@"
