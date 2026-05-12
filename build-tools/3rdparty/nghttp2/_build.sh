#!/bin/bash
# Builds nghttp2 (nghttp2/nghttp2) for $TARGET_PLATFORM. HTTP/2 framing
# library — consumed by libcurl for h2.
#
# Native build: assumes the caller is already running on (or inside a
# QEMU-emulated container of) the target architecture. The workflow
# arranges this — ubuntu-24.04-arm for linux-aarch64, debian:bookworm-
# slim under QEMU for linux-riscv64, native macos runners for macos-*.
# No cross-compilation. No Nix.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | linux-riscv64 |
#                     macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Output tarball layout (consumed by libcurl/_build.sh):
#   lib/libnghttp2.a
#   lib/pkgconfig/libnghttp2.pc
#   include/nghttp2/*.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-nghttp2-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL="https://github.com/nghttp2/nghttp2/releases/download/v${VERSION}/nghttp2-${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/nghttp2-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/nghttp2-${VERSION}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/nghttp2-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    echo "==> downloading nghttp2 ${VERSION}"
    _part="$TARBALL_CACHE.part.$$"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
    mv "$_part" "$TARBALL_CACHE"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"
fi
rm -rf "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

echo "==> configuring nghttp2 ${VERSION} for $TARGET_PLATFORM"
(cd "$SRC_DIR" && \
    ./configure --prefix="$INSTALL_DIR" \
        --enable-static --disable-shared \
        --enable-lib-only \
        --without-libxml2 --without-jansson \
        CFLAGS="-fPIC" CXXFLAGS="-fPIC")

echo "==> building nghttp2"
make -C "$SRC_DIR" -j"$NCPU"
make -C "$SRC_DIR" install

mkdir -p "$STAGE/lib" "$STAGE/include"
cp -a "$INSTALL_DIR/lib/libnghttp2.a" "$STAGE/lib/"
cp -a "$INSTALL_DIR/include/nghttp2" "$STAGE/include/"
if [ -d "$INSTALL_DIR/lib/pkgconfig" ]; then
    mkdir -p "$STAGE/lib/pkgconfig"
    cp -a "$INSTALL_DIR/lib/pkgconfig/." "$STAGE/lib/pkgconfig/"
fi

[ -f "$STAGE/lib/libnghttp2.a" ] || { echo "missing libnghttp2.a" >&2; exit 1; }
[ -f "$STAGE/include/nghttp2/nghttp2.h" ] || { echo "missing nghttp2.h" >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "nghttp2 $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
