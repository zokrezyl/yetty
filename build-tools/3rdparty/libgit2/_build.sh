#!/bin/bash
# Builds libgit2 for $TARGET_PLATFORM as a static library, for local-
# filesystem repository work only: no network transports (HTTPS, SSH,
# NTLM), the builtin regex + http-parser, and the bundled zlib
# (USE_BUNDLED_ZLIB=ON) so the archive is self-contained. The resulting
# libgit2.a needs only libc + pthread (+ librt on glibc for
# clock_gettime) — see build-tools/yetty/libs/libgit2.cmake.
#
# License posture: libgit2 is GPLv2 with a linking exception that
# explicitly permits combining with code under any other license. We do
# NOT modify libgit2 sources; a future patch, were one needed, would be
# distributable under GPLv2 the same way the unmodified library is.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | linux-riscv64 |
#                     macos-arm64 | macos-x86_64 | windows-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Output tarball layout (consumed by build-tools/yetty/libs/libgit2.cmake):
#   lib/libgit2.a
#   include/git2.h
#   include/git2/...

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libgit2-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL="https://github.com/libgit2/libgit2/archive/refs/tags/v${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/libgit2-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/libgit2-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libgit2-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            echo "==> downloading libgit2 ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.libgit2-download.lock"
    rm -f "$_part"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    # libgit2's tree carries symlinks under tests/resources/ that some
    # hosts (Windows, no symlink privilege) cannot recreate. We build with
    # BUILD_TESTS=OFF, so exclude the test tree for a portable extract.
    tar -C "$WORK_DIR" --exclude='*/tests/*' -xzf "$TARBALL_CACHE"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTS=OFF
    -DBUILD_CLI=OFF
    -DBUILD_EXAMPLES=OFF
    # Local-filesystem only: no network transports, no auth backends.
    -DUSE_HTTPS=OFF
    -DUSE_SSH=OFF
    -DUSE_NTLMCLIENT=OFF
    # Self-contained archive: bundled zlib + builtin regex/http-parser, so
    # libgit2.a resolves against nothing but libc + pthread (+ rt).
    -DUSE_BUNDLED_ZLIB=ON
    -DREGEX_BACKEND=builtin
    -DUSE_HTTP_PARSER=builtin
)

case "$TARGET_PLATFORM" in
linux-x86_64) : ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
    ) ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=riscv64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
    ) ;;
# macOS: libgit2 1.8.4's bundled zlib (deps/zlib) does not compile against
# recent macOS SDKs — its zutil.h '#define fdopen(fd,mode) NULL' poisons the
# SDK stdio.h fdopen prototype. Use the always-present system zlib instead
# (the later -D overrides the global -DUSE_BUNDLED_ZLIB=ON). Consumers link
# -lz on Apple — see build-tools/yetty/libs/libgit2.cmake.
macos-x86_64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64" "-DUSE_BUNDLED_ZLIB=OFF") ;;
macos-arm64)  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64"  "-DUSE_BUNDLED_ZLIB=OFF") ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cmake auto-
    # detects cl.exe with the Ninja generator.
    : ;;
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring libgit2 ${VERSION} for $TARGET_PLATFORM"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$NCPU"
cmake --install "$BUILD_DIR"

mkdir -p "$STAGE/lib" "$STAGE/include"
# libgit2's install lays down lib/libgit2.a (git2.lib on MSVC) +
# include/git2.h + include/git2/. Stage exactly those.
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        find "$INSTALL_DIR/$_D" -maxdepth 1 \
            \( -name 'libgit2.a' -o -name 'git2.lib' \) \
            -exec cp -a {} "$STAGE/lib/" \;
    fi
done
cp -a "$INSTALL_DIR/include/." "$STAGE/include/"

[ -f "$STAGE/lib/libgit2.a" ] || [ -f "$STAGE/lib/git2.lib" ] || {
    echo "missing libgit2.a / git2.lib in stage" >&2; find "$INSTALL_DIR" >&2; exit 1; }
[ -f "$STAGE/include/git2.h" ] || { echo "missing git2.h in stage" >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "libgit2 $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
# sed (not head) reads the whole stream, so `tar` never gets SIGPIPE — under
# `set -o pipefail` an early-closing `head` would fail the whole script.
tar -tzf "$TARBALL" | sed -n '1,20p'
