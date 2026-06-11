#!/bin/bash
# Builds yyjson (ibireme/yyjson) for $TARGET_PLATFORM. Single .c + .h
# pair; cmake-based upstream. Same per-platform pattern as tinyxml2.
#
# Output tarball layout (consumed by build-tools/yetty/libs/yyjson.cmake):
#   lib/libyyjson.a
#   include/yyjson.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-yyjson-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL="https://github.com/ibireme/yyjson/archive/refs/tags/${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/yyjson-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/yyjson-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/yyjson-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            echo "==> downloading yyjson ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.yyjson-download.lock"
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
    # Force lib (not lib64) so the cmake config we ship references
    # paths that line up with our staged layout.
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DYYJSON_BUILD_TESTS=OFF
    -DYYJSON_BUILD_DOC=OFF
    -DYYJSON_BUILD_MISC=OFF
    -DYYJSON_BUILD_FUZZER=OFF
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
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
    ) ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=riscv64"
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
        "-DANDROID_NDK=${ANDROID_NDK_HOME}"
    ) ;;
webasm|webasm-mt)
    EMCMAKE_PREFIX="emcmake"
    if [ "$TARGET_PLATFORM" = "webasm-mt" ]; then
        # -pthread on emscripten enables atomics + bulk-memory wasm
        # features so the resulting .a links into yetty.wasm built
        # with --shared-memory (USE_PTHREADS).
        CMAKE_ARGS+=("-DCMAKE_C_FLAGS=-pthread" "-DCMAKE_CXX_FLAGS=-pthread")
    fi
    ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell.
    : # cmake's default Ninja+cl pickup is fine
    ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring yyjson ${VERSION} for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$NCPU"
cmake --install "$BUILD_DIR"

mkdir -p "$STAGE/lib" "$STAGE/include"
for _D in lib lib64; do
    [ -d "$INSTALL_DIR/$_D" ] && find "$INSTALL_DIR/$_D" -maxdepth 1 \
        \( -name 'libyyjson*.a' -o -name 'yyjson*.lib' \) \
        -exec cp -a {} "$STAGE/lib/" \;
done
cp "$INSTALL_DIR/include/yyjson.h" "$STAGE/include/"

# Ship upstream's cmake config so consumers using find_package(yyjson
# CONFIG) can resolve. Lives under lib/cmake/yyjson/.
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D/cmake/yyjson" ]; then
        mkdir -p "$STAGE/lib/cmake/yyjson"
        cp -a "$INSTALL_DIR/$_D/cmake/yyjson/." "$STAGE/lib/cmake/yyjson/"
    fi
done

if [ ! -f "$STAGE/lib/libyyjson.a" ] && [ ! -f "$STAGE/lib/yyjson.lib" ]; then
    echo "missing libyyjson.a / yyjson.lib" >&2
    exit 1
fi
[ -f "$STAGE/include/yyjson.h" ] || { echo "missing yyjson.h" >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "yyjson $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
