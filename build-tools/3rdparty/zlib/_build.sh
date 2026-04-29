#!/bin/bash
# Builds zlib (madler/zlib) for $TARGET_PLATFORM via its upstream
# CMake. Plain zlib (not zlib-ng) — ABI-equivalent, simpler matrix.
#
# Output tarball layout (consumed by build-tools/cmake/libs/zlib.cmake):
#   lib/libz.a
#   include/zlib.h, zconf.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-zlib-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL="https://github.com/zlib-ng/zlib-ng/archive/refs/tags/${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/zlib-ng-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/zlib-ng-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/zlib-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            echo "==> downloading zlib ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.zlib-download.lock"
    rm -f "$_part"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"


CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    # zlib-ng with ZLIB_COMPAT acts as a drop-in zlib (libz.a, zlib.h
    # ABI-compatible) — same flag yetty's previous from-source build
    # used. ZLIBNG_ENABLE_TESTS / WITH_GTEST off keeps the build small.
    -DZLIB_COMPAT=ON
    -DZLIB_ENABLE_TESTS=OFF
    -DZLIBNG_ENABLE_TESTS=OFF
    -DWITH_GTEST=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
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
webasm)
    EMCMAKE_PREFIX="emcmake"
    # functable.c (the runtime SIMD-vs-scalar dispatch) miscompiles
    # under emscripten 4.0.23 — produces wasm with i32 left on the
    # operand stack at function-end ("type mismatch, expected [] got
    # [i32]") that wasm-emscripten-finalize rejects. There's nothing
    # to dispatch on wasm anyway (no SIMD/non-SIMD split for the
    # browser to pick from), so disable the dispatch entirely.
    CMAKE_ARGS+=("-DWITH_RUNTIME_CPU_DETECTION=OFF")
    ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cmake auto-
    # detects cl.exe.
    : # cmake's default Ninja+cl pickup is fine
    ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring zlib-ng ${VERSION} (compat) for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$NCPU"
cmake --install "$BUILD_DIR"

mkdir -p "$STAGE/lib" "$STAGE/include"
# zlib's static archive is libz.a on Unix, zlibstatic.lib on Windows
# (we only need the static lib, not the shared one).
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        find "$INSTALL_DIR/$_D" -maxdepth 1 \
            \( -name 'libz.a' -o -name 'libzlibstatic.a' -o -name 'zlibstatic.lib' -o -name 'zlib.lib' \) \
            -exec cp -a {} "$STAGE/lib/" \;
    fi
done
# Some configurations install only zlibstatic — alias to libz.a.
if [ ! -f "$STAGE/lib/libz.a" ] && [ -f "$STAGE/lib/libzlibstatic.a" ]; then
    cp "$STAGE/lib/libzlibstatic.a" "$STAGE/lib/libz.a"
fi
# Stage all installed headers — zlib-ng compat mode installs:
#   zlib.h, zconf.h, zlib_name_mangling.h (the third is referenced by
#   zconf.h #include "zlib_name_mangling.h"; missing it makes downstream
#   consumers like libpng fail with "No such file or directory").
cp -a "$INSTALL_DIR/include/." "$STAGE/include/"

# On Unix the static lib is libz.a; on Windows MSVC it's zlibstatic.lib
# (or zlib.lib for some configurations).
if [ ! -f "$STAGE/lib/libz.a" ] \
    && [ ! -f "$STAGE/lib/zlibstatic.lib" ] \
    && [ ! -f "$STAGE/lib/zlib.lib" ]; then
    echo "missing zlib static lib in stage" >&2
    find "$INSTALL_DIR" >&2
    exit 1
fi
[ -f "$STAGE/include/zlib.h" ]  || { echo "missing zlib.h"  >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "zlib $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
