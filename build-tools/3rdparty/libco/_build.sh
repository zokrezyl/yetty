#!/bin/bash
# Builds libco (higan-emu/libco) for $TARGET_PLATFORM. Single .c file,
# platform-appropriate stack-switch backend selected by libco itself
# via #ifdefs (amd64/aarch64/arm/x86/ppc → tiny inline asm; otherwise
# ucontext fallback).
#
# windows-x86_64 is intentionally absent — see build.sh for the why.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Version is read from this directory's `version` file. libco doesn't
# tag releases — pin via commit SHA.
#
# Output tarball layout (consumed by libs/co.cmake):
#   lib/libco.a (or co.lib on windows native MSVC)
#   include/libco.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libco-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# GitHub archive URL accepts commit SHAs as refs.
LIBCO_URL="https://github.com/higan-emu/libco/archive/${VERSION}.tar.gz"
LIBCO_TARBALL="$CACHE_DIR/libco-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/libco-${VERSION}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libco-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch
#-----------------------------------------------------------------------------
if [ ! -f "$LIBCO_TARBALL" ]; then
    _part="$LIBCO_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$LIBCO_TARBALL" ]; then
            echo "==> downloading libco @${VERSION:0:8}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$LIBCO_URL"
            mv "$_part" "$LIBCO_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.libco-download.lock"
    rm -f "$_part"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    mkdir -p "$WORK_DIR/.extract-$$"
    tar -C "$WORK_DIR/.extract-$$" -xzf "$LIBCO_TARBALL"
    mv "$WORK_DIR/.extract-$$/libco-${VERSION}" "$SRC_DIR"
    rmdir "$WORK_DIR/.extract-$$"
fi
rm -rf "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR/lib" "$INSTALL_DIR/include" "$STAGE"

#-----------------------------------------------------------------------------
# Per-platform compiler. libco is one .c file — no autotools, no cmake.
#-----------------------------------------------------------------------------
CFLAGS_BASE="-O2 -fPIC -std=c99"
CC=cc
AR=ar
CFLAGS_EXTRA=""

case "$TARGET_PLATFORM" in
linux-x86_64)
    CC=gcc
    ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"
    AR="${CROSS_PREFIX}ar"
    ;;
linux-riscv64)
    # libco has no riscv64 inline-asm backend — falls through to its
    # ucontext fallback in libco.c, which is fine on glibc Linux.
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"
    AR="${CROSS_PREFIX}ar"
    ;;
macos-x86_64)
    CC=clang
    CFLAGS_EXTRA="-arch x86_64"
    ;;
macos-arm64)
    CC=clang
    CFLAGS_EXTRA="-arch arm64"
    ;;
ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64)
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"
    : "${IOS_MIN:=15.0}"
    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)   _IOS_SDK="iphoneos";         _IOS_ARCH="arm64"
                     _MIN_FLAG="-miphoneos-version-min=${IOS_MIN}" ;;
        ios-x86_64)  _IOS_SDK="iphonesimulator";  _IOS_ARCH="x86_64"
                     _MIN_FLAG="-mios-simulator-version-min=${IOS_MIN}" ;;
        tvos-x86_64) _IOS_SDK="appletvsimulator"; _IOS_ARCH="x86_64"
                     _MIN_FLAG="-mtvos-simulator-version-min=${TVOS_MIN}" ;;
        tvos-arm64)  _IOS_SDK="appletvos"       ; _IOS_ARCH="arm64"
                     _MIN_FLAG="-mtvos-version-min=${TVOS_MIN}" ;;
    esac
    _IOS_SYSROOT="$(/usr/bin/xcrun --sdk "$_IOS_SDK" --show-sdk-path)"
    [ -d "$_IOS_SYSROOT" ] || { echo "missing SDK: $_IOS_SYSROOT" >&2; exit 1; }
    CC="/usr/bin/clang"
    AR="/usr/bin/ar"
    CFLAGS_EXTRA="-arch $_IOS_ARCH -isysroot $_IOS_SYSROOT $_MIN_FLAG"
    ;;
android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _NDK_TRIPLE="aarch64-linux-android" ;;
        android-x86_64)    _NDK_TRIPLE="x86_64-linux-android"  ;;
    esac
    CC="${_NDK_TRIPLE}${ANDROID_API}-clang"
    AR="llvm-ar"
    command -v "$CC" >/dev/null 2>&1 || {
        echo "error: $CC not on PATH (NDK shellHook expected)" >&2
        exit 1
    }
    ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cl.exe + lib.exe.
    CC=cl
    AR=lib
    CFLAGS_BASE="/nologo /O2 /MT /D_CRT_SECURE_NO_WARNINGS"
    CFLAGS_EXTRA=""
    ;;
*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

CFLAGS="$CFLAGS_BASE $CFLAGS_EXTRA"

#-----------------------------------------------------------------------------
# Compile + archive
#-----------------------------------------------------------------------------
echo "==> compiling libco for $TARGET_PLATFORM"
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    _SRC_W=$(cygpath -w "$SRC_DIR/libco.c")
    _OBJ_W=$(cygpath -w "$WORK_DIR/libco.obj")
    _OUT_W=$(cygpath -w "$INSTALL_DIR/lib/libco.lib")
    _SRC_DIR_W=$(cygpath -w "$SRC_DIR")
    MSYS2_ARG_CONV_EXCL='*' \
        $CC $CFLAGS "/I${_SRC_DIR_W}" /c "$_SRC_W" "/Fo${_OBJ_W}"
    MSYS2_ARG_CONV_EXCL='*' \
        $AR /nologo "/OUT:${_OUT_W}" "${_OBJ_W}"
else
    $CC $CFLAGS -I"$SRC_DIR" -c "$SRC_DIR/libco.c" -o "$WORK_DIR/libco.o"
    $AR rcs "$INSTALL_DIR/lib/libco.a" "$WORK_DIR/libco.o"
fi
cp "$SRC_DIR/libco.h" "$INSTALL_DIR/include/"

#-----------------------------------------------------------------------------
# Stage + verify
#-----------------------------------------------------------------------------
cp -a "$INSTALL_DIR/lib"     "$STAGE/"
cp -a "$INSTALL_DIR/include" "$STAGE/"

if [ ! -f "$STAGE/lib/libco.a" ] && [ ! -f "$STAGE/lib/libco.lib" ]; then
    echo "missing libco.a / libco.lib in stage" >&2
    exit 1
fi
[ -f "$STAGE/include/libco.h" ] || { echo "missing libco.h in stage" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "libco @${VERSION:0:8} ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
