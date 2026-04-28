#!/bin/bash
# Builds libuv (libuv/libuv) for $TARGET_PLATFORM using the upstream
# CMake project. Replaces the per-yetty-configure CPMAddPackage(libuv …)
# in build-tools/cmake/libs/libuv.cmake.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 | tvos-x86_64 |
#                     webasm | windows-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Version is read from this directory's `version` file.
#
# Optional env:
#   WORK_DIR          default /tmp/yetty-3rdparty-libuv-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#   ANDROID_API       default 26
#   IOS_MIN           default 15.0
#   CROSS_PREFIX      default aarch64-unknown-linux-gnu- (linux-aarch64)
#
# Output tarball layout (consumed by build-tools/cmake/libs/libuv.cmake):
#   lib/libuv_a.a (or uv_a.lib on windows)
#   include/uv.h, uv/...

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libuv-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Upstream libuv: tag is v<VER> on github.com/libuv/libuv.
LIBUV_URL="https://github.com/libuv/libuv/archive/refs/tags/v${VERSION}.tar.gz"
LIBUV_TARBALL="$CACHE_DIR/libuv-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/libuv-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libuv-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch (shared across targets, flock-protected).
#-----------------------------------------------------------------------------
if [ ! -f "$LIBUV_TARBALL" ]; then
    _part="$LIBUV_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then
            flock -x 9
        fi
        if [ ! -f "$LIBUV_TARBALL" ]; then
            echo "==> downloading libuv ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$LIBUV_URL"
            mv "$_part" "$LIBUV_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.libuv-download.lock"
    rm -f "$_part"
else
    echo "==> using cached libuv source: $LIBUV_TARBALL"
fi

#-----------------------------------------------------------------------------
# Extract — out-of-source build, shared SRC_DIR safe across targets.
#-----------------------------------------------------------------------------
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$LIBUV_TARBALL"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

#-----------------------------------------------------------------------------
# Per-platform CMake args.
#-----------------------------------------------------------------------------
CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    # libuv's own shared-build switch — disabling it produces ONLY libuv.a
    # (and skips libuv.so.*). Matches the existing yetty consumer flags.
    -DLIBUV_BUILD_SHARED=OFF
    -DLIBUV_BUILD_TESTS=OFF
    -DLIBUV_BUILD_BENCH=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
EMCMAKE_PREFIX=""

case "$TARGET_PLATFORM" in

linux-x86_64)
    : # native gcc
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
    )
    ;;

macos-x86_64)
    CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64")
    ;;

macos-arm64)
    CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")
    ;;

ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64)
    # nix-on-macOS xcrun trap.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"
    : "${IOS_MIN:=15.0}"
    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)
            _IOS_SDK="iphoneos";         _IOS_ARCH="arm64"
            _CMAKE_SYS="iOS";  _CMAKE_DEPL="$IOS_MIN"
            ;;
        ios-x86_64)
            _IOS_SDK="iphonesimulator";  _IOS_ARCH="x86_64"
            _CMAKE_SYS="iOS";  _CMAKE_DEPL="$IOS_MIN"
            ;;
        tvos-x86_64)
            _IOS_SDK="appletvsimulator"; _IOS_ARCH="x86_64"
            _CMAKE_SYS="tvOS"; _CMAKE_DEPL="$TVOS_MIN"
            ;;
        tvos-arm64)
            _IOS_SDK="appletvsimulator"; _IOS_ARCH="arm64"
            _CMAKE_SYS="tvOS"; _CMAKE_DEPL="$TVOS_MIN"
            ;;
    esac
    _IOS_SYSROOT="$(/usr/bin/xcrun --sdk "$_IOS_SDK" --show-sdk-path)"
    [ -d "$_IOS_SYSROOT" ] || { echo "missing SDK: $_IOS_SYSROOT" >&2; exit 1; }
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=$_CMAKE_SYS"
        "-DCMAKE_OSX_ARCHITECTURES=$_IOS_ARCH"
        "-DCMAKE_OSX_SYSROOT=$_IOS_SYSROOT"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=$_CMAKE_DEPL"
        "-DCMAKE_C_COMPILER=/usr/bin/clang"
        "-DCMAKE_CXX_COMPILER=/usr/bin/clang++"
    )
    ;;

android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _ANDROID_ABI=arm64-v8a ;;
        android-x86_64)    _ANDROID_ABI=x86_64    ;;
    esac
    CMAKE_ARGS+=(
        "-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake"
        "-DANDROID_ABI=${_ANDROID_ABI}"
        "-DANDROID_PLATFORM=android-${ANDROID_API}"
        "-DANDROID_NDK=${ANDROID_NDK_HOME}"
    )
    ;;

webasm)
    command -v emcmake >/dev/null 2>&1 || {
        echo "error: emcmake not found — source the .#3rdparty-webasm shell" >&2
        exit 1
    }
    EMCMAKE_PREFIX="emcmake"
    ;;

windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cmake auto-
    # detects cl.exe. libuv's own CMakeLists upstream supports MSVC.
    : # cmake's default Ninja+cl pickup is fine
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Configure + build + install
#-----------------------------------------------------------------------------
echo "==> configuring libuv for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"

echo "==> building (-j${NCPU})"
cmake --build "$BUILD_DIR" -j"$NCPU"

echo "==> installing"
cmake --install "$BUILD_DIR"

#-----------------------------------------------------------------------------
# Stage + verify. libuv installs:
#   - lib/libuv_a.a (uv_a.lib on Windows)
#   - include/uv.h + include/uv/*
#-----------------------------------------------------------------------------
mkdir -p "$STAGE/lib"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        cp -a "$INSTALL_DIR/$_D/." "$STAGE/lib/"
    fi
done
cp -a "$INSTALL_DIR/include" "$STAGE/"

if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    # Native MSVC: cmake's static-lib output is libuv.lib; older configs
    # produced uv_a.lib. Accept both.
    _LIB_CANDIDATES=("$STAGE/lib/libuv.lib" "$STAGE/lib/uv_a.lib" "$STAGE/lib/libuv_a.a" "$STAGE/lib/libuv.a")
else
    _LIB_CANDIDATES=("$STAGE/lib/libuv_a.a" "$STAGE/lib/libuv.a")
fi
_LIB_FOUND=""
for _LIB in "${_LIB_CANDIDATES[@]}"; do
    if [ -f "$_LIB" ]; then _LIB_FOUND="$_LIB"; break; fi
done
if [ -z "$_LIB_FOUND" ]; then
    echo "missing libuv static lib (tried: ${_LIB_CANDIDATES[*]})" >&2
    echo "stage tree:" >&2
    find "$STAGE" -maxdepth 4 -print >&2 || true
    exit 1
fi

if [ ! -f "$STAGE/include/uv.h" ]; then
    echo "missing headers: $STAGE/include/uv.h" >&2
    exit 1
fi

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "libuv $VERSION ($TARGET_PLATFORM) ready (lib: $(basename "$_LIB_FOUND")):"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 20 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,20p'
