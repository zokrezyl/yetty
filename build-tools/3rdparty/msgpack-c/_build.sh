#!/bin/bash
# Builds msgpack-c (msgpack/msgpack-c, c_master branch — pure C library)
# for $TARGET_PLATFORM via the upstream CMake project. Replaces the
# per-yetty-configure CPMAddPackage(msgpack-c …) in
# build-tools/cmake/libs/msgpack.cmake.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 | tvos-x86_64 |
#                     webasm | windows-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Version is read from this directory's `version` file. Upstream uses
# the unusual tag form `c-X.Y.Z` (the C library is a separate branch);
# the version file holds the full tag verbatim.
#
# Optional env:
#   WORK_DIR          default /tmp/yetty-3rdparty-msgpack-c-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#   ANDROID_API       default 26
#   IOS_MIN           default 15.0
#   CROSS_PREFIX      default aarch64-unknown-linux-gnu-
#
# Output tarball layout (consumed by libs/msgpack.cmake):
#   lib/libmsgpackc.a (or msgpackc.lib on Windows MSVC)
#   include/msgpack.h, include/msgpack/*.h

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

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-msgpack-c-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Upstream tag is `c-X.Y.Z` verbatim. GitHub archive top-level dir is
# `msgpack-c-c-X.Y.Z` (repo name + tag).
MSGPACK_URL="https://github.com/msgpack/msgpack-c/archive/refs/tags/${VERSION}.tar.gz"
MSGPACK_TARBALL="$CACHE_DIR/msgpack-c-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/msgpack-c-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/msgpack-c-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch
#-----------------------------------------------------------------------------
if [ ! -f "$MSGPACK_TARBALL" ]; then
    _part="$MSGPACK_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$MSGPACK_TARBALL" ]; then
            echo "==> downloading msgpack-c ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$MSGPACK_URL"
            mv "$_part" "$MSGPACK_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.msgpack-c-download.lock"
    rm -f "$_part"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    mkdir -p "$WORK_DIR/.extract-$$"
    tar -C "$WORK_DIR/.extract-$$" -xzf "$MSGPACK_TARBALL"
    # GitHub archive top-level dir for tag `c-X.Y.Z` is `msgpack-c-c-X.Y.Z`.
    mv "$WORK_DIR/.extract-$$/msgpack-c-${VERSION}" "$SRC_DIR"
    rmdir "$WORK_DIR/.extract-$$"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

#-----------------------------------------------------------------------------
# Per-platform CMake args. msgpack-c's CMakeLists.txt is plain — flags are
# the standard MSGPACK_BUILD_* + standard cross args.
#-----------------------------------------------------------------------------
CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DMSGPACK_BUILD_DOCS=OFF
    -DMSGPACK_BUILD_TESTS=OFF
    -DMSGPACK_BUILD_EXAMPLES=OFF
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
    )
    ;;

macos-x86_64)
    CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64")
    ;;

macos-arm64)
    CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")
    ;;

ios-arm64|ios-x86_64|tvos-x86_64)
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
    # detects cl.exe.
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
echo "==> configuring msgpack-c ${VERSION} for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"

echo "==> building (-j${NCPU})"
cmake --build "$BUILD_DIR" -j"$NCPU"

echo "==> installing"
cmake --install "$BUILD_DIR"

#-----------------------------------------------------------------------------
# Stage + verify
#-----------------------------------------------------------------------------
mkdir -p "$STAGE/lib"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        cp -a "$INSTALL_DIR/$_D/." "$STAGE/lib/"
    fi
done
cp -a "$INSTALL_DIR/include" "$STAGE/"

# Accept libmsgpackc.a (linux/macos/MSYS2) or msgpackc.lib (windows native MSVC).
_LIB_FOUND=""
for _CAND in "$STAGE/lib/libmsgpackc.a" "$STAGE/lib/libmsgpack-c.a" "$STAGE/lib/msgpack-c.lib" "$STAGE/lib/msgpackc.lib"; do
    if [ -f "$_CAND" ]; then _LIB_FOUND="$_CAND"; break; fi
done
if [ -z "$_LIB_FOUND" ]; then
    echo "missing msgpack-c static lib in stage" >&2
    find "$STAGE" -maxdepth 4 -print >&2 || true
    exit 1
fi
if [ ! -f "$STAGE/include/msgpack.h" ]; then
    echo "missing headers: $STAGE/include/msgpack.h" >&2
    exit 1
fi

# Drop CMake config + pkgconfig — the consumer cmake doesn't use them.
rm -rf "$STAGE/lib/cmake" "$STAGE/lib/pkgconfig"

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "msgpack-c $VERSION ($TARGET_PLATFORM) ready (lib: $(basename "$_LIB_FOUND")):"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 20 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,20p'
