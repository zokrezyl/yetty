#!/bin/bash
# Builds freetype (freetype/freetype) for $TARGET_PLATFORM via its
# upstream CMake. Optional deps disabled (harfbuzz/brotli/bzip2/png) —
# yetty's consumer cmake wires those in separately. zlib IS enabled
# because yetty embeds compressed PCF/SFNT fonts; we link against the
# prebuilt zlib tarball at build time so freetype.a's .gz path resolves.
#
# Output tarball layout (consumed by build-tools/cmake/libs/freetype.cmake):
#   lib/libfreetype.a
#   include/freetype2/...        (matches upstream install)
#   include/ft2build.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
ZLIB_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/zlib/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-freetype-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"

# Upstream tags use the form "VER-2-13-2"
TAG="VER-$(echo "$VERSION" | tr '.' '-')"
URL="https://github.com/freetype/freetype/archive/refs/tags/${TAG}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/freetype-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/freetype-${TAG}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/freetype-${TARGET_PLATFORM}-${VERSION}.tar.gz"

ZLIB_TAR_URL="$URL_BASE/lib-zlib-${ZLIB_VERSION}/zlib-${TARGET_PLATFORM}-${ZLIB_VERSION}.tar.gz"
ZLIB_TARBALL="$CACHE_DIR/zlib-${TARGET_PLATFORM}-${ZLIB_VERSION}.tar.gz"
ZLIB_PREFIX="$WORK_DIR/zlib-${TARGET_PLATFORM}-${ZLIB_VERSION}"

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

fetch "$URL"          "$TARBALL_CACHE" "freetype ${VERSION} (${TAG})"            freetype-source
fetch "$ZLIB_TAR_URL" "$ZLIB_TARBALL"  "zlib ${ZLIB_VERSION} (${TARGET_PLATFORM})" freetype-zlib

if [ ! -d "$SRC_DIR" ]; then tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"; fi
# Always re-extract the prebuilt dep — see libpng's _build.sh for why.
rm -rf "$ZLIB_PREFIX"
mkdir -p "$ZLIB_PREFIX"
tar -C "$ZLIB_PREFIX" -xzf "$ZLIB_TARBALL"

rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DFT_DISABLE_HARFBUZZ=ON
    -DFT_DISABLE_BROTLI=ON
    -DFT_DISABLE_BZIP2=ON
    -DFT_DISABLE_PNG=ON
    -DFT_DISABLE_ZLIB=OFF
    -DFT_REQUIRE_ZLIB=ON
    -DZLIB_ROOT="$ZLIB_PREFIX"
    -DZLIB_INCLUDE_DIR="$ZLIB_PREFIX/include"
    -DZLIB_LIBRARY="$ZLIB_PREFIX/lib/libz.a"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
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
webasm) EMCMAKE_PREFIX="emcmake" ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cmake auto-
    # detects cl.exe.
    : # cmake's default Ninja+cl pickup is fine
    ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring freetype ${VERSION} for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$NCPU"
cmake --install "$BUILD_DIR"

mkdir -p "$STAGE/lib" "$STAGE/include"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        find "$INSTALL_DIR/$_D" -maxdepth 1 \
            \( -name 'libfreetype*.a' -o -name 'freetype*.lib' \) \
            -exec cp -a {} "$STAGE/lib/" \;
    fi
done
cp -a "$INSTALL_DIR/include/." "$STAGE/include/"

if [ ! -f "$STAGE/lib/libfreetype.a" ] && [ ! -f "$STAGE/lib/freetype.lib" ]; then
    echo "missing libfreetype.a / freetype.lib" >&2
    find "$INSTALL_DIR" >&2
    exit 1
fi
[ -f "$STAGE/include/ft2build.h" ] || \
[ -f "$STAGE/include/freetype2/ft2build.h" ] || { echo "missing ft2build.h" >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "freetype $VERSION ($TARGET_PLATFORM, zlib $ZLIB_VERSION) ready:"
ls -lh "$TARBALL"
echo "contents (first 30):"
{ tar -tzf "$TARBALL" | head -30; } || true
