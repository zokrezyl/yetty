#!/bin/bash
# Builds libjpeg-turbo for $TARGET_PLATFORM via its upstream CMake.
# Uses NASM for SIMD on x86_64 / AVX2 — nasm is in the relevant
# 3rdparty-* nix shells already.
#
# Output tarball layout (consumed by build-tools/cmake/libs/libjpeg-turbo.cmake):
#   lib/libjpeg.a
#   lib/libturbojpeg.a
#   include/jpeglib.h, jconfig.h, jmorecfg.h, jerror.h, turbojpeg.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libjpeg-turbo-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL="https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/libjpeg-turbo-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/libjpeg-turbo-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libjpeg-turbo-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            echo "==> downloading libjpeg-turbo ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.libjpeg-turbo-download.lock"
    rm -f "$_part"
fi

if [ ! -d "$SRC_DIR" ]; then tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"; fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DENABLE_SHARED=OFF
    -DENABLE_STATIC=ON
    -DWITH_TURBOJPEG=ON
    -DWITH_JPEG8=ON
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
        "-DCMAKE_SYSTEM_PROCESSOR=$_IOS_ARCH"
        "-DCMAKE_OSX_ARCHITECTURES=$_IOS_ARCH"
        "-DCMAKE_OSX_SYSROOT=$_IOS_SYSROOT"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=$_CMAKE_DEPL"
        "-DCMAKE_C_COMPILER=/usr/bin/clang"
        # libjpeg-turbo's NASM SIMD path doesn't have an iOS/tvOS toolchain
        # description that matches our cross setup — disable SIMD on
        # iOS/tvOS to skip the nasm probe.
        "-DWITH_SIMD=OFF"
        "-DREQUIRE_SIMD=OFF"
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
    # webasm has no SIMD path in libjpeg-turbo.
    CMAKE_ARGS+=("-DWITH_SIMD=OFF") ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell.
    : # cmake's default Ninja+cl pickup is fine
    ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring libjpeg-turbo ${VERSION} for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$NCPU"
cmake --install "$BUILD_DIR"

mkdir -p "$STAGE/lib" "$STAGE/include"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        find "$INSTALL_DIR/$_D" -maxdepth 1 \
            \( -name 'libjpeg.a' -o -name 'libturbojpeg.a' \
               -o -name 'jpeg-static.lib' -o -name 'turbojpeg-static.lib' \
               -o -name 'jpeg.lib' -o -name 'turbojpeg.lib' \) \
            -exec cp -a {} "$STAGE/lib/" \;
    fi
done
for _h in jpeglib.h jconfig.h jmorecfg.h jerror.h turbojpeg.h; do
    [ -f "$INSTALL_DIR/include/$_h" ] && cp "$INSTALL_DIR/include/$_h" "$STAGE/include/" || true
done

# Unix: lib{jpeg,turbojpeg}.a; Windows MSVC: {jpeg,turbojpeg}-static.lib.
for _name in jpeg turbojpeg; do
    if [ ! -f "$STAGE/lib/lib${_name}.a" ] \
        && [ ! -f "$STAGE/lib/${_name}-static.lib" ] \
        && [ ! -f "$STAGE/lib/${_name}.lib" ]; then
        echo "missing $_name static lib in stage" >&2
        find "$INSTALL_DIR" >&2
        exit 1
    fi
done
[ -f "$STAGE/include/jpeglib.h" ]  || { echo "missing jpeglib.h" >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "libjpeg-turbo $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
