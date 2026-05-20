#!/usr/bin/env bash
# miniaudio 3rdparty wrapper. Single-header public-domain (MIT-0)
# audio I/O library; the same miniaudio.h serves every target with the
# backend (WASAPI / CoreAudio / ALSA / PulseAudio / AAudio / OpenSL ES
# / WebAudio) selected inside the header at compile time. Even though
# the artefact is identical across platforms, we publish per-platform
# tarballs so the file naming matches every other 3rdparty producer
# (lz4, bzip2, etc.) and the cmake fetcher resolves
# miniaudio-<platform>-<version>.tar.gz uniformly.
#
# Required env:
#   TARGET_PLATFORM  platform slug (linux-x86_64, macos-arm64, …)
#   OUTPUT_DIR       where the tarball is written

set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|linux-riscv64|macos-x86_64|macos-arm64|\
    android-arm64-v8a|android-x86_64|ios-arm64|ios-x86_64|\
    tvos-arm64|tvos-x86_64|webasm|webasm-mt)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM%-mt}" ;;
    windows-x86_64)
        # No compilation here — just curl + tar — so we don't actually
        # need MSVC. Drop straight into _build.sh on the host shell.
        exec bash "$(dirname "$0")/_build.sh" "$@" ;;
    *) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

[ "${USE_NIX:-1}" = "0" ] && exec bash "$(dirname "$0")/_build.sh" "$@"
cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/miniaudio/_build.sh "$@"
