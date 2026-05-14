#!/usr/bin/env bash
# openssh 3rdparty wrapper. Builds against the prebuilt openssl-new tarball
# from build-tools/3rdparty/openssl-new/ — see _build.sh for the cross-fetch
# of that asset at build time.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64  | macos-x86_64
#   OUTPUT_DIR        where the tarball is written

set -euo pipefail

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|\
    macos-x86_64|macos-arm64)
        SHELL_NAME="3rdparty-${TARGET_PLATFORM}"
        ;;
    *)
        echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
        exit 1
        ;;
esac

if [ "${USE_NIX:-1}" = "0" ]; then
    exec bash "$(dirname "$0")/_build.sh" "$@"
fi

cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/openssh/_build.sh "$@"
