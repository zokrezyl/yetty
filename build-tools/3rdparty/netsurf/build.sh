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
# Same matrix as the other 3rdparty libs (lexbor/libcurl/zlib): every
# platform routes to the matching `3rdparty-${TARGET_PLATFORM}` nix dev
# shell (which provides cross compiler + target-side deps) and re-execs
# into _build.sh. NetSurf's Makefile is Unix-centric, so cross-compile
# to Windows/webasm is best-effort; _build.sh bails per-platform when
# the toolchain isn't compatible.
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64)
        # Native linux build needs gperf/flex/bison/expat/libxml2/jpeg/
        # png/webp — kept in a dedicated shell so we don't pollute the
        # generic 3rdparty-linux-x86_64.
        SHELL_NAME="3rdparty-netsurf"
        ;;
    linux-aarch64|linux-riscv64|\
    macos-x86_64|macos-arm64|\
    android-arm64-v8a|android-x86_64|\
    ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64|\
    webasm)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}"
        ;;
    windows-x86_64)
        # Native MSVC: caller must have vcvarsall'd the shell so cl.exe
        # is on PATH. NetSurf's Makefile + MSVC are mutually allergic
        # — _build.sh's windows branch bails with a clear message.
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
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/netsurf/_build.sh "$@"
