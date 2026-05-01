#!/bin/bash
# Builds bzip2 (libarchive/bzip2) for $TARGET_PLATFORM. Upstream isn't a
# CMake project; we compile its 7 .c files into a static archive
# (mirrors the from-source bundle in build-tools/cmake/FreeType.cmake).
#
# Output tarball layout (consumed by build-tools/cmake/libs/bzip2.cmake):
#   lib/libbz2.a
#   include/bzlib.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-bzip2-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"

URL="https://github.com/libarchive/bzip2/archive/refs/tags/bzip2-${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/bzip2-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/bzip2-bzip2-${VERSION}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/bzip2-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.bzip2-download.lock"
    rm -f "$_part"
fi

if [ ! -d "$SRC_DIR" ]; then
    tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/include"

CFLAGS_BASE="-O2 -fPIC -DNDEBUG -w"
CC=cc
AR=ar
CFLAGS_EXTRA=""

case "$TARGET_PLATFORM" in
linux-x86_64) CC=gcc ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"; AR="${CROSS_PREFIX}ar" ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"; AR="${CROSS_PREFIX}ar" ;;
macos-x86_64) CC=clang; CFLAGS_EXTRA="-arch x86_64" ;;
macos-arm64)  CC=clang; CFLAGS_EXTRA="-arch arm64"  ;;
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
    CC=/usr/bin/clang; AR=/usr/bin/ar
    CFLAGS_EXTRA="-arch $_IOS_ARCH -isysroot $_IOS_SYSROOT $_MIN_FLAG"
    ;;
android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _T="aarch64-linux-android" ;;
        android-x86_64)    _T="x86_64-linux-android"  ;;
    esac
    CC="${_T}${ANDROID_API}-clang"; AR="llvm-ar" ;;
webasm|webasm-mt)
    CC=emcc; AR=emar
    if [ "$TARGET_PLATFORM" = "webasm-mt" ]; then
        # -pthread enables atomics + bulk-memory; required for linking
        # into yetty.wasm built with --shared-memory.
        CFLAGS_EXTRA="$CFLAGS_EXTRA -pthread"
    fi
    ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cl.exe + lib.exe.
    # MSYS2_ARG_CONV_EXCL='*' is applied locally on each cl/lib call
    # below so MSVC /flags pass through. Do NOT export it globally —
    # git-bash on windows-latest needs MSYS conversion for the curl
    # invocations that fetch source tarballs.
    CC=cl
    AR=lib
    CFLAGS_BASE="/nologo /O2 /MT /D_CRT_SECURE_NO_WARNINGS"
    CFLAGS_EXTRA=""
    ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

CFLAGS="$CFLAGS_BASE $CFLAGS_EXTRA"
SOURCES=(blocksort.c huffman.c crctable.c randtable.c compress.c decompress.c bzlib.c)

OBJS=()
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    # MSVC: cl /c /Fo<obj> <src>; lib /OUT:<lib> <objs>
    # cygpath -w converts /tmp/... -> C:\msys64\tmp\... so cl sees a real
    # Windows path (it would otherwise treat the leading / as a flag).
    # MSYS2_ARG_CONV_EXCL='*' applied per-call so MSVC /flags pass
    # through without affecting the curl/tar/etc. invocations elsewhere.
    _SRC_DIR_W=$(cygpath -w "$SRC_DIR")
    for _s in "${SOURCES[@]}"; do
        _o="$WORK_DIR/${_s%.c}-${TARGET_PLATFORM}.obj"
        _o_w=$(cygpath -w "$_o")
        _src_w=$(cygpath -w "$SRC_DIR/$_s")
        MSYS2_ARG_CONV_EXCL='*' \
            $CC $CFLAGS "/I${_SRC_DIR_W}" /c "$_src_w" "/Fo${_o_w}"
        OBJS+=("$_o")
    done
    _OUT_W=$(cygpath -w "$STAGE/lib/libbz2.lib")
    _OBJS_W=()
    for _o in "${OBJS[@]}"; do _OBJS_W+=("$(cygpath -w "$_o")"); done
    MSYS2_ARG_CONV_EXCL='*' \
        $AR /nologo "/OUT:${_OUT_W}" "${_OBJS_W[@]}"
else
    for _s in "${SOURCES[@]}"; do
        _o="$WORK_DIR/${_s%.c}-${TARGET_PLATFORM}.o"
        $CC $CFLAGS -I"$SRC_DIR" -c "$SRC_DIR/$_s" -o "$_o"
        OBJS+=("$_o")
    done
    $AR rcs "$STAGE/lib/libbz2.a" "${OBJS[@]}"
fi
cp "$SRC_DIR/bzlib.h" "$STAGE/include/"

if [ ! -f "$STAGE/lib/libbz2.a" ] && [ ! -f "$STAGE/lib/libbz2.lib" ]; then
    echo "missing libbz2.a / libbz2.lib" >&2
    exit 1
fi
[ -f "$STAGE/include/bzlib.h" ] || { echo "missing bzlib.h"  >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "bzip2 $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
