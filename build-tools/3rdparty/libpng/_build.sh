#!/bin/bash
# Builds libpng (pnggroup/libpng) for $TARGET_PLATFORM via its upstream
# CMake. Depends on zlib at build time — we fetch the prebuilt zlib
# tarball published by build-3rdparty-zlib.yml.
#
# Output tarball layout (consumed by build-tools/cmake/libs/libpng.cmake):
#   lib/libpng.a (or libpng16.a — installer alias names)
#   include/png.h, pngconf.h, pnglibconf.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

ZLIB_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/zlib/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libpng-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"

URL="https://github.com/pnggroup/libpng/archive/refs/tags/v${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/libpng-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/libpng-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libpng-${TARGET_PLATFORM}-${VERSION}.tar.gz"

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

fetch "$URL"          "$TARBALL_CACHE" "libpng ${VERSION}"                       libpng-source
fetch "$ZLIB_TAR_URL" "$ZLIB_TARBALL"  "zlib ${ZLIB_VERSION} (${TARGET_PLATFORM}, build-time dep)" libpng-zlib

if [ ! -d "$SRC_DIR" ]; then tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"; fi
# Always re-extract the prebuilt dep — a stale tree from a previous
# producer run with an older zlib tarball silently breaks the build.
rm -rf "$ZLIB_PREFIX"
mkdir -p "$ZLIB_PREFIX"
tar -C "$ZLIB_PREFIX" -xzf "$ZLIB_TARBALL"

# Resolve the actual zlib static archive path inside the prebuilt
# tarball — Unix produces lib/libz.a, MSVC produces lib/zlibstatic.lib
# (or lib/zlib.lib).
ZLIB_LIB=""
for _candidate in libz.a zlibstatic.lib zlib.lib; do
    if [ -f "$ZLIB_PREFIX/lib/$_candidate" ]; then
        ZLIB_LIB="$ZLIB_PREFIX/lib/$_candidate"
        break
    fi
done
[ -n "$ZLIB_LIB" ] || { echo "missing zlib static lib in $ZLIB_PREFIX/lib/" >&2; ls -la "$ZLIB_PREFIX/lib/" >&2; exit 1; }
[ -f "$ZLIB_PREFIX/include/zlib.h" ] || { echo "missing zlib.h" >&2; exit 1; }

rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DPNG_SHARED=OFF
    -DPNG_STATIC=ON
    -DPNG_TESTS=OFF
    -DPNG_TOOLS=OFF
    -DPNG_FRAMEWORK=OFF
    -DSKIP_INSTALL_PROGRAMS=ON
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    # Compile every libpng object with hidden visibility. libpng's PNG_EXPORT
    # only attaches default visibility on shared (PNG_DLL) builds — for
    # PNG_STATIC=ON it expands to nothing, so the global preset wins and all
    # png_* symbols become STV_HIDDEN. They are still resolvable at static-
    # link time inside the consuming executable, but never reach its dynamic
    # symbol table — which is what stops libdecor's GTK plugin from rejecting
    # itself with "conflicting symbol png_free" on Wayland (issue #107).
    -DCMAKE_C_VISIBILITY_PRESET=hidden
    -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON
    -DZLIB_ROOT="$ZLIB_PREFIX"
    -DZLIB_INCLUDE_DIR="$ZLIB_PREFIX/include"
    -DZLIB_LIBRARY="$ZLIB_LIB"
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
        "-DPNG_HARDWARE_OPTIMIZATIONS=OFF"
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
        "-DPNG_HARDWARE_OPTIMIZATIONS=OFF"
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
        "-DPNG_HARDWARE_OPTIMIZATIONS=OFF"
    ) ;;
webasm) EMCMAKE_PREFIX="emcmake"; CMAKE_ARGS+=("-DPNG_HARDWARE_OPTIMIZATIONS=OFF") ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cmake auto-
    # detects cl.exe.
    : # cmake's default Ninja+cl pickup is fine
    ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring libpng ${VERSION} for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$NCPU"
cmake --install "$BUILD_DIR"

mkdir -p "$STAGE/lib" "$STAGE/include"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        find "$INSTALL_DIR/$_D" -maxdepth 1 \
            \( -name 'libpng*.a' -o -name 'libpng*_static.lib' -o -name 'libpng*.lib' \) \
            -exec cp -a {} "$STAGE/lib/" \;
    fi
done
# Provide a libpng.a alias pointing at libpng16.a for consumers that
# expect the unversioned name.
if [ ! -f "$STAGE/lib/libpng.a" ] && [ -f "$STAGE/lib/libpng16.a" ]; then
    cp "$STAGE/lib/libpng16.a" "$STAGE/lib/libpng.a"
fi
cp "$INSTALL_DIR/include/png.h" "$STAGE/include/"
cp "$INSTALL_DIR/include/pngconf.h" "$STAGE/include/"
cp "$INSTALL_DIR/include/pnglibconf.h" "$STAGE/include/"

# On Unix the static lib is libpng.a / libpng16.a; on Windows MSVC it's
# libpng16_static.lib (or libpng16.lib).
if [ ! -f "$STAGE/lib/libpng.a" ] \
    && [ ! -f "$STAGE/lib/libpng16.a" ] \
    && ! ls "$STAGE/lib/"libpng*_static.lib >/dev/null 2>&1 \
    && ! ls "$STAGE/lib/"libpng*.lib >/dev/null 2>&1; then
    echo "missing libpng static lib in stage" >&2
    find "$INSTALL_DIR" >&2
    exit 1
fi
[ -f "$STAGE/include/png.h" ]  || { echo "missing png.h"   >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "libpng $VERSION ($TARGET_PLATFORM, zlib $ZLIB_VERSION) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
