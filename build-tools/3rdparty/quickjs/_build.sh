#!/bin/bash
# Builds quickjs-ng (quickjs-ng/quickjs) for $TARGET_PLATFORM via its
# upstream CMake project. Pure C / libc-only — no transitive prebuilt
# deps, so the script mirrors lexbor/_build.sh rather than
# libcurl/_build.sh.
#
# We build ONLY the `qjs` static library target — not the qjs/qjsc CLI
# executables. The CLI pulls in quickjs-libc (os/syscalls we don't bind
# into the JS host) and, more importantly, would need host execution
# during cross builds. The pre-generated unicode tables shipped in the
# source tree mean `--target qjs` never runs a host-built generator, so
# every cross target (android / webasm / ios / tvos / riscv) builds
# cleanly the same way the native one does.
#
# Output tarball layout (consumed by build-tools/yetty/libs/quickjs.cmake):
#   lib/libqjs.a            (Unix)
#   lib/qjs.lib             (Windows MSVC)
#   include/quickjs.h       (+ sibling quickjs-*.h)

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "$SCRIPT_DIR/version is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-quickjs-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Upstream quickjs-ng: tag is v<ver>; tarball top-level dir is quickjs-<ver>.
URL="https://github.com/quickjs-ng/quickjs/archive/refs/tags/v${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/quickjs-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/quickjs-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/quickjs-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            echo "==> downloading quickjs-ng ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.quickjs-download.lock"
    rm -f "$_part"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"
fi
rm -rf "$BUILD_DIR" "$STAGE"
mkdir -p "$STAGE"

# Static lib only; everything optional off. Mirrors the option set that
# upstream's CMakeLists exposes (see CMakeLists.txt at tag v${VERSION}).
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTING=OFF
    -DQJS_ENABLE_INSTALL=OFF
    -DQJS_BUILD_EXAMPLES=OFF
    -DQJS_BUILD_CLI_STATIC=OFF
    -DQJS_BUILD_CLI_WITH_MIMALLOC=OFF
    -DQJS_BUILD_CLI_WITH_STATIC_MIMALLOC=OFF
    -DQJS_BUILD_LIBC=OFF
    -DQJS_BUILD_WERROR=OFF
    -DQJS_DISABLE_PARSER=OFF
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
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=riscv64"
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
    [ -d "$_IOS_SYSROOT" ] || { echo "missing SDK: $_IOS_SYSROOT" >&2; exit 1; }
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=$_CMAKE_SYS"
        "-DCMAKE_OSX_ARCHITECTURES=$_IOS_ARCH"
        "-DCMAKE_OSX_SYSROOT=$_IOS_SYSROOT"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=$_CMAKE_DEPL"
        "-DCMAKE_C_COMPILER=/usr/bin/clang"
    ) ;;
android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
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
webasm)
    command -v emcmake >/dev/null 2>&1 || {
        echo "error: emcmake not found — source the .#3rdparty-webasm shell" >&2
        exit 1
    }
    EMCMAKE_PREFIX="emcmake"
    ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cmake auto-
    # detects cl.exe.
    : # cmake's default Ninja+cl pickup is fine
    ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring quickjs-ng ${VERSION} for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"

echo "==> building qjs (-j${NCPU})"
cmake --build "$BUILD_DIR" -j"$NCPU" --target qjs

#-----------------------------------------------------------------------------
# Stage + verify. quickjs-ng's `qjs` target lands as libqjs.a (Unix) /
# qjs.lib (Windows MSVC) somewhere under the build tree; the public
# headers live at the source root.
#-----------------------------------------------------------------------------
mkdir -p "$STAGE/lib" "$STAGE/include"

_LIB_SRC=""
for _NAME in libqjs.a qjs.lib; do
    _FOUND="$(find "$BUILD_DIR" -name "$_NAME" -type f -print -quit 2>/dev/null || true)"
    if [ -n "$_FOUND" ]; then _LIB_SRC="$_FOUND"; break; fi
done
if [ -z "$_LIB_SRC" ]; then
    echo "missing qjs static lib under $BUILD_DIR" >&2
    find "$BUILD_DIR" -maxdepth 3 -name '*.a' -o -name '*.lib' >&2 || true
    exit 1
fi
cp -a "$_LIB_SRC" "$STAGE/lib/"

# Public headers — quickjs.h is the API surface ybrowser includes via
# <quickjs.h>; copy any sibling quickjs-*.h so transitive includes
# resolve regardless of upstream layout churn.
cp -a "$SRC_DIR"/quickjs.h "$STAGE/include/"
for _H in "$SRC_DIR"/quickjs-*.h; do
    [ -f "$_H" ] && cp -a "$_H" "$STAGE/include/"
done

if [ ! -f "$STAGE/include/quickjs.h" ]; then
    echo "missing header: $STAGE/include/quickjs.h" >&2
    exit 1
fi

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "quickjs-ng $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 25 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,25p'
