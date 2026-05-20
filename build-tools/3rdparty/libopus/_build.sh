#!/bin/bash
# Builds libopus (xiph/opus) for $TARGET_PLATFORM and packages it as a
# tarball under $OUTPUT_DIR. Produces an installable bundle (lib/ +
# include/) that 3rdparty-fetch.cmake can drop into the build tree.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | linux-riscv64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 | tvos-arm64 | tvos-x86_64 |
#                     webasm | windows-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Version is read from this directory's `version` file — single source of
# truth for both upstream tag fetch (v<VER>) and tarball naming.
#
# Optional env:
#   WORK_DIR          default /tmp/yetty-3rdparty-libopus-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#   ANDROID_API       default 26 (matches yetty Android build)
#   IOS_MIN           default 15.0
#   TVOS_MIN          default 17.0
#   CROSS_PREFIX      default aarch64-unknown-linux-gnu- (linux-aarch64)

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libopus-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# xiph tags upstream releases as `vX.Y.Z` — version file holds the bare X.Y.Z.
OPUS_URL="https://github.com/xiph/opus/archive/refs/tags/v${VERSION}.tar.gz"
OPUS_TARBALL="$CACHE_DIR/libopus-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/opus-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libopus-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch (shared across targets) — flock so parallel target builds share a
# single download.
#-----------------------------------------------------------------------------
if [ ! -f "$OPUS_TARBALL" ]; then
    _part="$OPUS_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$OPUS_TARBALL" ]; then
            echo "==> downloading libopus ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$OPUS_URL"
            mv "$_part" "$OPUS_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.libopus-download.lock"
    rm -f "$_part"
else
    echo "==> using cached libopus source: $OPUS_TARBALL"
fi

#-----------------------------------------------------------------------------
# Extract — out-of-source cmake, one shared SRC_DIR across targets is fine.
#-----------------------------------------------------------------------------
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$OPUS_TARBALL"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

#-----------------------------------------------------------------------------
# Common cmake flags. We turn off everything that needs an exe runner
# (programs, tests, doc) — only the static library + headers are kept.
#-----------------------------------------------------------------------------
COMMON_FLAGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_INSTALL_LIBDIR=lib
    -DBUILD_SHARED_LIBS=OFF
    -DOPUS_BUILD_PROGRAMS=OFF
    -DOPUS_BUILD_TESTING=OFF
    -DOPUS_BUILD_SHARED_LIBRARY=OFF
    -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF
    -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=ON
)
EXTRA_FLAGS=()
CMAKE_GEN=("-G" "Ninja")
CMAKE_WRAPPER=""

case "$TARGET_PLATFORM" in

linux-x86_64)
    :
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    TOOLCHAIN="$WORK_DIR/toolchain-linux-aarch64.cmake"
    cat > "$TOOLCHAIN" <<TC
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)
set(CMAKE_AR ${CROSS_PREFIX}ar)
set(CMAKE_RANLIB ${CROSS_PREFIX}ranlib)
set(CMAKE_STRIP ${CROSS_PREFIX}strip)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
TC
    EXTRA_FLAGS+=("-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN")
    ;;

linux-riscv64)
    # libopus has hand-written ARM/x86 intrinsics paths — there is no
    # RISC-V backend. The build falls back to the portable C path
    # automatically when neon/sse aren't detected, so no flag flip needed.
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    TOOLCHAIN="$WORK_DIR/toolchain-linux-riscv64.cmake"
    cat > "$TOOLCHAIN" <<TC
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_C_COMPILER ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)
set(CMAKE_AR ${CROSS_PREFIX}ar)
set(CMAKE_RANLIB ${CROSS_PREFIX}ranlib)
set(CMAKE_STRIP ${CROSS_PREFIX}strip)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
TC
    EXTRA_FLAGS+=("-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN")
    ;;

macos-x86_64)
    EXTRA_FLAGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64")
    ;;

macos-arm64)
    EXTRA_FLAGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")
    ;;

android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _ANDROID_ABI=arm64-v8a ;;
        android-x86_64)    _ANDROID_ABI=x86_64    ;;
    esac
    EXTRA_FLAGS+=(
        "-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake"
        "-DANDROID_ABI=${_ANDROID_ABI}"
        "-DANDROID_PLATFORM=android-${ANDROID_API}"
        "-DANDROID_STL=c++_static"
    )
    ;;

ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64)
    # Same nix-on-macOS xcrun trap as openh264/dav1d — see those scripts.
    # /usr/bin first so any bare-`xcrun` shell-out hits Apple's, not nix's.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"

    : "${IOS_MIN:=15.0}"
    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)
            _SYSNAME=iOS;   _SDK=iphoneos;        _ARCH=arm64
            _MIN_VAR=CMAKE_OSX_DEPLOYMENT_TARGET; _MIN_VAL=$IOS_MIN
            ;;
        ios-x86_64)
            _SYSNAME=iOS;   _SDK=iphonesimulator; _ARCH=x86_64
            _MIN_VAR=CMAKE_OSX_DEPLOYMENT_TARGET; _MIN_VAL=$IOS_MIN
            ;;
        tvos-arm64)
            _SYSNAME=tvOS;  _SDK=appletvos;        _ARCH=arm64
            _MIN_VAR=CMAKE_OSX_DEPLOYMENT_TARGET; _MIN_VAL=$TVOS_MIN
            ;;
        tvos-x86_64)
            _SYSNAME=tvOS;  _SDK=appletvsimulator; _ARCH=x86_64
            _MIN_VAR=CMAKE_OSX_DEPLOYMENT_TARGET; _MIN_VAL=$TVOS_MIN
            ;;
    esac
    _SYSROOT="$(/usr/bin/xcrun --sdk "$_SDK" --show-sdk-path)"
    [ -d "$_SYSROOT" ] || { echo "missing SDK: $_SYSROOT" >&2; exit 1; }
    EXTRA_FLAGS+=(
        "-DCMAKE_SYSTEM_NAME=${_SYSNAME}"
        "-DCMAKE_OSX_SYSROOT=${_SYSROOT}"
        "-DCMAKE_OSX_ARCHITECTURES=${_ARCH}"
        "-D${_MIN_VAR}=${_MIN_VAL}"
        # libopus 1.5 default-enables Neon intrinsic detection on aarch64
        # at compile time. iOS arm64 has Neon mandatory, so the autodetect
        # path is fine — but the *runtime* feature probe (getauxval on
        # Linux, sysctlbyname on Darwin) compiles cleanly on iOS too.
    )
    ;;

webasm)
    command -v emcmake >/dev/null 2>&1 || {
        echo "error: emcmake not found — source the .#3rdparty-webasm shell" >&2
        exit 1
    }
    CMAKE_WRAPPER="emcmake"
    EXTRA_FLAGS+=(
        "-DCMAKE_C_FLAGS=-pthread"
        "-DCMAKE_CXX_FLAGS=-pthread"
    )
    ;;

windows-x86_64)
    # Native MSVC under vcvarsall. The runner's cmake picks "Ninja" as
    # generator since we ship ninja in the 3rdparty shell — but for
    # MSVC-on-Windows the proper invocation is `-G Ninja` with cl.exe
    # already on PATH via vcvarsall.
    EXTRA_FLAGS+=(
        "-DCMAKE_C_COMPILER=cl"
        "-DCMAKE_CXX_COMPILER=cl"
    )
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Configure + build + install
#-----------------------------------------------------------------------------
echo "==> configuring libopus for $TARGET_PLATFORM"
echo "    flags: ${COMMON_FLAGS[*]} ${EXTRA_FLAGS[*]}"

if [ -n "$CMAKE_WRAPPER" ]; then
    "$CMAKE_WRAPPER" cmake -S "$SRC_DIR" -B "$BUILD_DIR" "${CMAKE_GEN[@]}" \
        "${COMMON_FLAGS[@]}" "${EXTRA_FLAGS[@]}"
else
    cmake -S "$SRC_DIR" -B "$BUILD_DIR" "${CMAKE_GEN[@]}" \
        "${COMMON_FLAGS[@]}" "${EXTRA_FLAGS[@]}"
fi

echo "==> building (-j${NCPU})"
cmake --build "$BUILD_DIR" --parallel "$NCPU"

echo "==> installing"
cmake --install "$BUILD_DIR"

#-----------------------------------------------------------------------------
# Verify install layout
#-----------------------------------------------------------------------------
case "$TARGET_PLATFORM" in
    windows-x86_64) _LIB="$INSTALL_DIR/lib/opus.lib"  ;;
    *)              _LIB="$INSTALL_DIR/lib/libopus.a" ;;
esac
if [ ! -f "$_LIB" ]; then
    echo "missing library: $_LIB" >&2
    echo "install tree:" >&2
    find "$INSTALL_DIR" -maxdepth 4 -print >&2 || true
    exit 1
fi
if [ ! -f "$INSTALL_DIR/include/opus/opus.h" ]; then
    echo "missing headers: $INSTALL_DIR/include/opus/" >&2
    exit 1
fi

#-----------------------------------------------------------------------------
# Stage + package — keep the install layout (lib/, include/) so the
# 3rdparty-fetch consumer can use the same imported-target pattern.
#-----------------------------------------------------------------------------
cp -a "$INSTALL_DIR/lib"     "$STAGE/"
cp -a "$INSTALL_DIR/include" "$STAGE/"

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "libopus $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 20 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,20p'
