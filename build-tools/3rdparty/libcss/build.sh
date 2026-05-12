#!/usr/bin/env bash
# libcss 3rdparty wrapper. Builds NetSurf's MIT-licensed CSS cascade
# (libcss + libparserutils + libwapcaplet) standalone — no GPL'd
# NetSurf core, no other libcurl/xml/png/jpeg/webp deps. The same set
# of three libs is also produced by the netsurf 3rdparty package as a
# side-effect of building netsurf-all; this package is what stays once
# the netsurf prebuilt is dropped.
#
# Shell mapping mirrors netsurf's (the host deps the upstream make
# build needs — pkg-config + GNU make — are present in both the
# generic 3rdparty-<platform> shells and the netsurf-flavoured ones,
# so we reuse the netsurf shells when they exist):
#
#   linux-x86_64       → 3rdparty-netsurf
#   linux-aarch64      → 3rdparty-netsurf-linux-aarch64
#   linux-riscv64      → 3rdparty-netsurf-linux-riscv64
#   macos-arm64        → 3rdparty-netsurf-macos-arm64
#   macos-x86_64       → 3rdparty-netsurf-macos-x86_64
#   android-*, ios-*,  → no nix shell — _build.sh's per-platform branch
#   tvos-*, webasm,      writes a placeholder UNSUPPORTED tarball and
#   windows-x86_64       exits 0; the consumer-side libcss.cmake
#                        detects the marker and skips silently.
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64)
        SHELL_NAME="3rdparty-netsurf"
        ;;
    linux-aarch64|linux-riscv64|\
    macos-arm64|macos-x86_64)
        SHELL_NAME="3rdparty-netsurf-${TARGET_PLATFORM}"
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
