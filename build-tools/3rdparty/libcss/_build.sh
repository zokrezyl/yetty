#!/bin/bash
# Builds the VENDORED libcss tree (src/libcss: libcss 0.9.2 +
# libparserutils 0.2.5 + libwapcaplet 0.4.3, locally patched — see
# src/libcss/README.md) for $TARGET_PLATFORM via the standalone CMake
# project next to this script. Produces the prebuilt tarball that
# build-tools/yetty/libs/libcss.cmake consumes.
#
# Unlike the other 3rdparty producers there is NO upstream download:
# the source of truth is the patched tree vendored in this repo, so the
# CI workflow's checkout IS the source fetch. The version file carries
# the upstream version plus a -pN patch revision (0.9.2-p1); bump -pN
# whenever the vendored source changes.
#
# Output tarball layout (consumed by build-tools/yetty/libs/libcss.cmake):
#   lib/libcss.a  lib/libparserutils.a  lib/libwapcaplet.a
#   include/libcss/...  include/parserutils/...  include/libwapcaplet/...
#
# Platform matrix: real builds on linux-* and macos-* (same coverage the
# old in-tree build had — its guard was CMAKE_SYSTEM_NAME Linux|Darwin).
# Everything else ships a placeholder tarball carrying only an
# UNSUPPORTED marker; the consumer cmake detects it and skips silently,
# leaving ybrowser on its lexbor-CSS fallback path.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "$SCRIPT_DIR/version is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libcss-$TARGET_PLATFORM}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

SRC_ROOT="$REPO_ROOT/src/libcss"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libcss-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR"

[ -f "$SRC_ROOT/libcss/src/select/select.c" ] || {
    echo "vendored libcss source not found at $SRC_ROOT" >&2; exit 1; }

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
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

macos-x86_64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
macos-arm64)  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")  ;;

android-arm64-v8a|android-x86_64|\
ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64|\
webasm|windows-x86_64)
    # Same platform set the old in-tree build skipped (its guard was
    # CMAKE_SYSTEM_NAME Linux|Darwin). libparserutils calls
    # iconv_open/iconv/iconv_close (src/input/filter.c) which Android's
    # bionic and MSVC's CRT don't provide, and none of these targets
    # ever ran the libcss cascade — ybrowser uses its lexbor-CSS
    # fallback there. Ship a placeholder tarball carrying only an
    # UNSUPPORTED marker so the release matrix stays uniform; the
    # consumer-side libcss.cmake detects the marker and skips silently.
    echo "libcss: $TARGET_PLATFORM is not built (see UNSUPPORTED in tarball)" >&2
    rm -rf "$STAGE"
    mkdir -p "$STAGE"
    cat > "$STAGE/UNSUPPORTED" <<EOF
libcss $VERSION is not built for $TARGET_PLATFORM.

The vendored NetSurf libcss stack builds only for Linux and macOS —
the same coverage the old in-tree CMake build had. libparserutils
depends on iconv (absent from Android bionic and MSVC), and ybrowser
falls back to its lexbor-CSS path on every platform in this list.

The yetty consumer side (build-tools/yetty/libs/libcss.cmake) detects
this UNSUPPORTED marker and skips target creation silently — the rest
of yetty configures unaffected.
EOF
    tar -C "$STAGE" -czf "$TARBALL" .
    echo "libcss $VERSION ($TARGET_PLATFORM) — placeholder tarball written:"
    ls -lh "$TARBALL"
    exit 0
    ;;

*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

# The property-parser generator must RUN on the build host. In the cross
# shells plain `gcc` is sometimes the cross compiler, so pin the host
# compiler explicitly unless the caller already did.
case "$TARGET_PLATFORM" in
linux-aarch64|linux-riscv64)
    if [ -z "${LIBCSS_HOST_CC:-}" ]; then
        for _cand in cc gcc clang; do
            if command -v "$_cand" >/dev/null 2>&1; then
                LIBCSS_HOST_CC="$_cand"
                break
            fi
        done
    fi
    export LIBCSS_HOST_CC
    ;;
esac

echo "==> configuring libcss ${VERSION} for $TARGET_PLATFORM (vendored $SRC_ROOT)"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"

echo "==> building (-j${NCPU})"
cmake --build "$BUILD_DIR" -j"$NCPU"

echo "==> installing"
cmake --install "$BUILD_DIR"

#-----------------------------------------------------------------------------
# Stage + verify
#-----------------------------------------------------------------------------
cp -a "$INSTALL_DIR/." "$STAGE/"

for _required in \
    lib/libcss.a lib/libparserutils.a lib/libwapcaplet.a \
    include/libcss/libcss.h \
    include/parserutils/parserutils.h \
    include/libwapcaplet/libwapcaplet.h; do
    [ -f "$STAGE/$_required" ] || {
        echo "missing $_required in stage" >&2
        find "$STAGE" -maxdepth 3 -print >&2 || true
        exit 1
    }
done

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "libcss $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 25 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,25p'
