#!/usr/bin/env bash
# netsurf-all 3rdparty wrapper. NetSurf 3.11 (the source-full release —
# core browser + helper libs: libcss, libdom, libhubbub, libwapcaplet,
# libparserutils, libnsutils, libnsbmp, libnsgif, libnslog, libnspsl,
# libsvgtiny, libutf8proc).
#
# Built via NetSurf's own Makefile with TARGET=monkey (the smallest
# viable frontend that produces the full set of helper-lib archives +
# the core .o tree). yetty links the core .o files into its own
# libnetsurf_core.a (frontends/* excluded) and plugs its own ynetsurf
# frontend tables in place of monkey's.
#
# Cross-compile is non-trivial for monkey, so the matrix is Linux-only:
# linux-x86_64 (native). aarch64/riscv64/mac/win/android/webasm are
# OUT OF SCOPE for this prebuilt — yetty silently disables ynetsurf
# on those targets.
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64)
        # NetSurf needs expat/libxml2/libjpeg/libpng/libwebp at build
        # time — the generic 3rdparty-linux-x86_64 shell doesn't carry
        # those, so we use a dedicated shell.
        SHELL_NAME="3rdparty-netsurf"
        ;;
    linux-aarch64|linux-riscv64|\
    macos-x86_64|macos-arm64|\
    android-arm64-v8a|android-x86_64|\
    ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64|\
    webasm|windows-x86_64)
        echo "netsurf: $TARGET_PLATFORM is not supported (Linux-x86_64 only)" >&2
        exit 2
        ;;
    *) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

if [ "${USE_NIX:-1}" = "0" ]; then
    exec bash "$(dirname "$0")/_build.sh" "$@"
fi
cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/netsurf/_build.sh "$@"
