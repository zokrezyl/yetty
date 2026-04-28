#!/bin/bash
# Builds msdfgen (Chlumsky/msdfgen) for $TARGET_PLATFORM. Upstream's
# CMakeLists builds msdfgen-core (pure math, no deps) and msdfgen-ext
# (needs freetype + tinyxml2 for SVG/font import). We compile both via
# the same per-platform pattern as libuv/libssh2; the freetype+tinyxml2
# headers come from the prebuilt 3rdparty tarballs we just published.
#
# Output tarball layout (consumed by build-tools/cmake/libs/msdfgen.cmake):
#   lib/libmsdfgen-core.a
#   lib/libmsdfgen-ext.a
#   include/msdfgen/...
#   include/msdfgen-ext/...

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
FREETYPE_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/freetype/version")"
TINYXML2_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/tinyxml2/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-msdfgen-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"

URL="https://github.com/Chlumsky/msdfgen/archive/refs/tags/v${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/msdfgen-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/msdfgen-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/msdfgen-${TARGET_PLATFORM}-${VERSION}.tar.gz"

FT_TAR_URL="$URL_BASE/lib-freetype-${FREETYPE_VERSION}/freetype-${TARGET_PLATFORM}-${FREETYPE_VERSION}.tar.gz"
FT_TARBALL="$CACHE_DIR/freetype-${TARGET_PLATFORM}-${FREETYPE_VERSION}.tar.gz"
FT_PREFIX="$WORK_DIR/freetype-${TARGET_PLATFORM}-${FREETYPE_VERSION}"

TX_TAR_URL="$URL_BASE/lib-tinyxml2-${TINYXML2_VERSION}/tinyxml2-${TARGET_PLATFORM}-${TINYXML2_VERSION}.tar.gz"
TX_TARBALL="$CACHE_DIR/tinyxml2-${TARGET_PLATFORM}-${TINYXML2_VERSION}.tar.gz"
TX_PREFIX="$WORK_DIR/tinyxml2-${TARGET_PLATFORM}-${TINYXML2_VERSION}"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

fetch() {
    local url="$1" cache="$2" descr="$3" lock="$4"
    if [ ! -f "$cache" ]; then
        local part="$cache.part.$$"
        (
            if command -v flock >/dev/null 2>&1; then flock -x 9; fi
            if [ ! -f "$cache" ]; then
                echo "==> downloading $descr"
                curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$part" "$url"
                mv "$part" "$cache"
            fi
        ) 9>"$CACHE_DIR/.$lock.lock"
        rm -f "$part"
    fi
}

fetch "$URL"        "$TARBALL_CACHE" "msdfgen ${VERSION}" msdfgen-source
fetch "$FT_TAR_URL" "$FT_TARBALL"    "freetype ${FREETYPE_VERSION} (${TARGET_PLATFORM})" msdfgen-freetype
fetch "$TX_TAR_URL" "$TX_TARBALL"    "tinyxml2 ${TINYXML2_VERSION} (${TARGET_PLATFORM})" msdfgen-tinyxml2

if [ ! -d "$SRC_DIR" ];   then tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"; fi
# Always re-extract dep prefixes — the dep tarballs themselves are
# cache-keyed by version, but a stale extracted tree (from before the
# producer started shipping cmake config files, etc.) silently breaks
# downstream cmake's find_package. Cheap to redo.
rm -rf "$FT_PREFIX" "$TX_PREFIX"
mkdir -p "$FT_PREFIX" "$TX_PREFIX"
tar -C "$FT_PREFIX" -xzf "$FT_TARBALL"
tar -C "$TX_PREFIX" -xzf "$TX_TARBALL"

rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

# Find the freetype include dir — upstream installs to include/freetype2/
# but we ship include/ flattened. Try both.
_FT_INC="$FT_PREFIX/include"
[ -d "$FT_PREFIX/include/freetype2" ] && _FT_INC="$FT_PREFIX/include/freetype2"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DMSDFGEN_BUILD_STANDALONE=OFF
    -DMSDFGEN_USE_VCPKG=OFF
    -DMSDFGEN_USE_CPP11=ON
    -DMSDFGEN_USE_SKIA=OFF
    -DMSDFGEN_DISABLE_PNG=ON
    -DMSDFGEN_DISABLE_SVG=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    "-DFREETYPE_INCLUDE_DIRS=$_FT_INC;$FT_PREFIX/include"
    "-DFREETYPE_LIBRARY=$FT_PREFIX/lib/libfreetype.a"
    "-Dtinyxml2_INCLUDE_DIRS=$TX_PREFIX/include"
    "-Dtinyxml2_LIBRARIES=$TX_PREFIX/lib/libtinyxml2.a"
    # msdfgen uses find_package(tinyxml2 CONFIG) which needs the
    # config dir, not just include + lib paths.
    "-Dtinyxml2_DIR=$TX_PREFIX/lib/cmake/tinyxml2"
)
EMCMAKE_PREFIX=""

case "$TARGET_PLATFORM" in
linux-x86_64) : ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
    ) ;;
macos-x86_64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
macos-arm64)  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")  ;;
ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64)
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"
    : "${IOS_MIN:=15.0}"
    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)   _IOS_SDK="iphoneos";         _IOS_ARCH="arm64"
                     _CMAKE_SYS="iOS";  _CMAKE_DEPL="$IOS_MIN" ;;
        ios-x86_64)  _IOS_SDK="iphonesimulator";  _IOS_ARCH="x86_64"
                     _CMAKE_SYS="iOS";  _CMAKE_DEPL="$IOS_MIN" ;;
        tvos-x86_64) _IOS_SDK="appletvsimulator"; _IOS_ARCH="x86_64"
                     _CMAKE_SYS="tvOS"; _CMAKE_DEPL="$TVOS_MIN" ;;
        tvos-arm64)  _IOS_SDK="appletvos"       ; _IOS_ARCH="arm64"
                     _CMAKE_SYS="tvOS"; _CMAKE_DEPL="$TVOS_MIN" ;;
    esac
    _IOS_SYSROOT="$(/usr/bin/xcrun --sdk "$_IOS_SDK" --show-sdk-path)"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=$_CMAKE_SYS"
        "-DCMAKE_OSX_ARCHITECTURES=$_IOS_ARCH"
        "-DCMAKE_OSX_SYSROOT=$_IOS_SYSROOT"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=$_CMAKE_DEPL"
        "-DCMAKE_C_COMPILER=/usr/bin/clang"
        "-DCMAKE_CXX_COMPILER=/usr/bin/clang++"
    ) ;;
android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _ABI=arm64-v8a ;;
        android-x86_64)    _ABI=x86_64    ;;
    esac
    CMAKE_ARGS+=(
        "-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake"
        "-DANDROID_ABI=${_ABI}"
        "-DANDROID_PLATFORM=android-${ANDROID_API}"
    ) ;;
webasm) EMCMAKE_PREFIX="emcmake" ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring msdfgen ${VERSION} for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$NCPU"
cmake --install "$BUILD_DIR" || true

mkdir -p "$STAGE/lib" "$STAGE/include"
# The cmake install path is unstable across versions — pick up the .a's
# from the build dir directly.
find "$BUILD_DIR" -maxdepth 3 -name 'libmsdfgen*.a' -exec cp -a {} "$STAGE/lib/" \;
# Fallback: if build dir didn't yield anything, look in install.
if ! ls "$STAGE/lib/"libmsdfgen*.a >/dev/null 2>&1; then
    for _D in lib lib64; do
        [ -d "$INSTALL_DIR/$_D" ] && find "$INSTALL_DIR/$_D" -maxdepth 1 -name 'libmsdfgen*.a' -exec cp -a {} "$STAGE/lib/" \;
    done
fi

# Stage public headers preserving the upstream layout — msdfgen.h does
# `#include "core/base.h"` (and ~25 more core/-prefixed includes), and
# msdfgen-ext.h does `#include "ext/import-svg.h"`. Renaming core/→msdfgen/
# breaks that. Mirror upstream verbatim:
#   include/msdfgen.h
#   include/msdfgen-ext.h
#   include/core/*.h
#   include/ext/*.h
cp -a "$SRC_DIR/msdfgen.h"     "$STAGE/include/" 2>/dev/null || true
cp -a "$SRC_DIR/msdfgen-ext.h" "$STAGE/include/" 2>/dev/null || true
mkdir -p "$STAGE/include/core" "$STAGE/include/ext"
cp -a "$SRC_DIR/core/." "$STAGE/include/core/"
cp -a "$SRC_DIR/ext/."  "$STAGE/include/ext/"
# Strip .cpp files from the staged headers (we only want declarations).
find "$STAGE/include" -name '*.cpp' -delete 2>/dev/null || true

ls "$STAGE/lib/"libmsdfgen-core.a >/dev/null 2>&1 || { echo "missing libmsdfgen-core.a" >&2; find "$BUILD_DIR" "$INSTALL_DIR" 2>/dev/null >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "msdfgen $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
echo "contents (first 30):"
{ tar -tzf "$TARBALL" | head -30; } || true
