#!/bin/bash
# Builds lexbor (lexbor/lexbor) for $TARGET_PLATFORM via its upstream
# CMake project. Pure C / libc-only — no transitive prebuilt deps,
# so the script mirrors zlib/_build.sh rather than libcurl/_build.sh.
#
# Output tarball layout (consumed by build-tools/cmake/libs/lexbor.cmake):
#   lib/liblexbor_static.a        (Unix)
#   lib/lexbor_static.lib         (Windows MSVC)
#   include/lexbor/<module>/*.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "$SCRIPT_DIR/version is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-lexbor-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Upstream lexbor: tag is v<ver>; tarball top-level dir is lexbor-<ver>.
URL="https://github.com/lexbor/lexbor/archive/refs/tags/v${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/lexbor-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/lexbor-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/lexbor-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            echo "==> downloading lexbor ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.lexbor-download.lock"
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
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DLEXBOR_BUILD_SHARED=OFF
    -DLEXBOR_BUILD_STATIC=ON
    -DLEXBOR_BUILD_SEPARATELY=OFF
    -DLEXBOR_BUILD_TESTS=OFF
    -DLEXBOR_BUILD_EXAMPLES=OFF
    -DLEXBOR_BUILD_BENCHMARKS=OFF
    -DLEXBOR_BUILD_UTILS=OFF
    -DLEXBOR_INSTALL_HEADERS=ON
    -DLEXBOR_MAKE_RPM_FILES=OFF
    -DLEXBOR_MAKE_DEB_FILES=OFF
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

echo "==> configuring lexbor ${VERSION} for $TARGET_PLATFORM"
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

# Drop upstream's CMake/pkg-config metadata — yetty's consumer cmake
# imports the static lib directly and doesn't read lexbor-config.cmake.
rm -rf "$STAGE/lib/cmake" "$STAGE/lib/pkgconfig"

# Accept liblexbor_static.a (Unix) or lexbor_static.lib (Windows MSVC).
_LIB_FOUND=""
for _CAND in "$STAGE/lib/liblexbor_static.a" "$STAGE/lib/lexbor_static.lib"; do
    if [ -f "$_CAND" ]; then _LIB_FOUND="$_CAND"; break; fi
done
if [ -z "$_LIB_FOUND" ]; then
    echo "missing lexbor static lib in stage" >&2
    find "$STAGE" -maxdepth 4 -print >&2 || true
    exit 1
fi
if [ ! -f "$STAGE/include/lexbor/core/core.h" ]; then
    echo "missing headers: $STAGE/include/lexbor/core/core.h" >&2
    exit 1
fi

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "lexbor $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 25 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,25p'
