#!/bin/bash
# Builds yxml (Yorhel/yxml) for $TARGET_PLATFORM.
#
# yxml is a tiny streaming XML parser: a single .c + .h pair, MIT-licensed,
# no upstream tarball, no tags. We fetch the .c/.h via cgit's `/plain/`
# endpoint pinned to a commit SHA (more reliable than `git clone` — that
# endpoint has been throwing 429s + missing-blob errors).
#
# Output tarball layout (consumed by build-tools/yetty/libs/yxml.cmake):
#   lib/libyxml.a      (or yxml.lib on windows-x86_64)
#   include/yxml.h
#   COPYING

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "$SCRIPT_DIR/version is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-yxml-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"

# Upstream is hosted on cgit at g.blicky.net. The `/plain/<file>?id=<sha>`
# endpoint serves a single file at a pinned commit — way more reliable
# than `git clone` against that host (which intermittently fails with
# missing-blob / 429). Same source author Yorhel directs people to on
# https://dev.yorhel.nl/yxml.
UPSTREAM_BASE="https://g.blicky.net/yxml.git/plain"

# Short SHA for display only (the full SHA is in the version file).
SHORT="${VERSION:0:12}"

SRC_DIR="$WORK_DIR/src"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/yxml-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR" "$SRC_DIR"

#-----------------------------------------------------------------------------
# Fetch yxml.c + yxml.h + COPYING from cgit, cached by commit SHA so the
# matrix runs don't all hammer upstream and so subsequent local rebuilds
# are offline-clean. Files land in $CACHE_DIR/yxml-<sha>/.
#-----------------------------------------------------------------------------
CACHE_VER_DIR="$CACHE_DIR/yxml-${VERSION}"
mkdir -p "$CACHE_VER_DIR"

fetch_one() {
    local _name="$1"
    local _dst="$CACHE_VER_DIR/$_name"
    local _url="${UPSTREAM_BASE}/${_name}?id=${VERSION}"
    if [ -s "$_dst" ]; then return 0; fi
    local _part="$_dst.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -s "$_dst" ]; then
            echo "==> fetching $_name @${SHORT}"
            echo "    $_url"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                 -o "$_part" "$_url"
            mv "$_part" "$_dst"
        fi
    ) 9>"$CACHE_DIR/.yxml-${VERSION}.lock"
    rm -f "$_part"
}

fetch_one yxml.c
fetch_one yxml.h
fetch_one COPYING

cp "$CACHE_VER_DIR/yxml.c"   "$SRC_DIR/yxml.c"
cp "$CACHE_VER_DIR/yxml.h"   "$SRC_DIR/yxml.h"
cp "$CACHE_VER_DIR/COPYING"  "$SRC_DIR/COPYING"

# Sanity-check the .c we just pulled isn't a 0-byte or HTML-error.
head -c 4 "$SRC_DIR/yxml.c" | grep -q '/' || {
    echo "yxml.c looks bogus (no leading comment) — upstream fetch failed?" >&2
    head -c 200 "$SRC_DIR/yxml.c" >&2
    exit 1
}

rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/include"

# yxml's source is C99; one TU. Use position-independent code so the
# resulting .a links into shared/wasm/iOS-framework consumers.
# yxml.c uses `#include <yxml.h>` (angle brackets), so the header dir
# has to be on the include search path.
COMMON_CFLAGS="-O2 -fPIC -std=c99 -Wall -w -I$SRC_DIR"

#-----------------------------------------------------------------------------
# Per-platform compile. Pattern mirrors tinyxml2/_build.sh: choose CC/AR
# from the nix shell ($CC default for native shells, $CROSS_PREFIX-gcc/ar
# for cross shells, NDK clang for android, xcrun -sdk for darwin embedded,
# emcc/emar for webasm, cl/lib for windows-MSVC).
#-----------------------------------------------------------------------------
case "$TARGET_PLATFORM" in

linux-x86_64|macos-x86_64|macos-arm64)
    CC="${CC:-cc}"
    AR="${AR:-ar}"
    CFLAGS_EXTRA=""
    LDFLAGS_EXTRA=""
    case "$TARGET_PLATFORM" in
        macos-x86_64) CFLAGS_EXTRA="-arch x86_64" ;;
        macos-arm64)  CFLAGS_EXTRA="-arch arm64"  ;;
    esac
    echo "==> compiling yxml @${SHORT} for $TARGET_PLATFORM ($CC)"
    "$CC" $COMMON_CFLAGS $CFLAGS_EXTRA -c "$SRC_DIR/yxml.c" -o "$WORK_DIR/yxml.o"
    "$AR" rcs "$STAGE/lib/libyxml.a" "$WORK_DIR/yxml.o"
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CC="${CC:-${CROSS_PREFIX}gcc}"
    AR="${AR:-${CROSS_PREFIX}ar}"
    echo "==> cross-compiling yxml @${SHORT} for linux-aarch64 ($CC)"
    "$CC" $COMMON_CFLAGS -c "$SRC_DIR/yxml.c" -o "$WORK_DIR/yxml.o"
    "$AR" rcs "$STAGE/lib/libyxml.a" "$WORK_DIR/yxml.o"
    ;;

linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CC="${CC:-${CROSS_PREFIX}gcc}"
    AR="${AR:-${CROSS_PREFIX}ar}"
    echo "==> cross-compiling yxml @${SHORT} for linux-riscv64 ($CC)"
    "$CC" $COMMON_CFLAGS -c "$SRC_DIR/yxml.c" -o "$WORK_DIR/yxml.o"
    "$AR" rcs "$STAGE/lib/libyxml.a" "$WORK_DIR/yxml.o"
    ;;

android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set}"
    : "${ANDROID_API:=26}"
    NDK_HOST="$(uname -s | tr '[:upper:]' '[:lower:]')-x86_64"
    NDK_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$NDK_HOST/bin"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) NDK_TRIPLE="aarch64-linux-android" ;;
        android-x86_64)    NDK_TRIPLE="x86_64-linux-android"  ;;
    esac
    CC="$NDK_BIN/${NDK_TRIPLE}${ANDROID_API}-clang"
    AR="$NDK_BIN/llvm-ar"
    echo "==> cross-compiling yxml @${SHORT} for $TARGET_PLATFORM ($CC)"
    "$CC" $COMMON_CFLAGS -c "$SRC_DIR/yxml.c" -o "$WORK_DIR/yxml.o"
    "$AR" rcs "$STAGE/lib/libyxml.a" "$WORK_DIR/yxml.o"
    ;;

ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64)
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"
    : "${IOS_MIN:=15.0}"
    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)   _SDK="iphoneos";         _ARCH="arm64"  ; _MIN="$IOS_MIN"  ;;
        ios-x86_64)  _SDK="iphonesimulator";  _ARCH="x86_64" ; _MIN="$IOS_MIN"  ;;
        tvos-arm64)  _SDK="appletvos";        _ARCH="arm64"  ; _MIN="$TVOS_MIN" ;;
        tvos-x86_64) _SDK="appletvsimulator"; _ARCH="x86_64" ; _MIN="$TVOS_MIN" ;;
    esac
    _SYSROOT="$(/usr/bin/xcrun --sdk "$_SDK" --show-sdk-path)"
    case "$_SDK" in
        iphoneos|iphonesimulator) _MIN_FLAG="-mios-version-min=${_MIN}" ;;
        appletvos)                _MIN_FLAG="-mtvos-version-min=${_MIN}" ;;
        appletvsimulator)         _MIN_FLAG="-mtvos-simulator-version-min=${_MIN}" ;;
    esac
    CC=/usr/bin/clang
    AR=/usr/bin/ar
    echo "==> compiling yxml @${SHORT} for $TARGET_PLATFORM ($_SDK $_ARCH)"
    "$CC" $COMMON_CFLAGS -arch "$_ARCH" -isysroot "$_SYSROOT" $_MIN_FLAG \
         -c "$SRC_DIR/yxml.c" -o "$WORK_DIR/yxml.o"
    "$AR" rcs "$STAGE/lib/libyxml.a" "$WORK_DIR/yxml.o"
    ;;

webasm)
    command -v emcc >/dev/null 2>&1 || { echo "emcc not on PATH (need emscripten / nix .#3rdparty-webasm shell)" >&2; exit 1; }
    echo "==> compiling yxml @${SHORT} for webasm (emcc)"
    emcc $COMMON_CFLAGS -c "$SRC_DIR/yxml.c" -o "$WORK_DIR/yxml.o"
    emar rcs "$STAGE/lib/libyxml.a" "$WORK_DIR/yxml.o"
    ;;

windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell (cl.exe + lib.exe on PATH).
    # -MD = use the C runtime DLL (matches the rest of the yetty Windows
    # build); -O2 release-opt; -W0 silence yxml's intentional macro spew.
    # lib.exe builds a static yxml.lib from yxml.obj. We use `-flag`
    # instead of `/flag` because MSYS Git Bash on the GH runner mangles
    # `//flag` / `/flag` paths (it sees them as POSIX paths and rewrites
    # them — that's what bit us as `//OUT:/tmp/...` going through
    # unparsed). cl/lib accept both forms; `-` is the bash-safe one.
    command -v cl   >/dev/null 2>&1 || { echo "cl.exe not on PATH"  >&2; exit 1; }
    command -v lib  >/dev/null 2>&1 || { echo "lib.exe not on PATH" >&2; exit 1; }
    echo "==> compiling yxml @${SHORT} for windows-x86_64 (MSVC)"
    # Belt-and-braces: also tell MSYS not to convert path-like args.
    export MSYS2_ARG_CONV_EXCL='*'
    (
        cd "$WORK_DIR"
        cl -nologo -c -O2 -MD -W0 -I "$SRC_DIR" -Fo:yxml.obj "$SRC_DIR/yxml.c"
        lib -nologo "-OUT:$STAGE/lib/yxml.lib" yxml.obj
    )
    ;;

*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

#-----------------------------------------------------------------------------
# Stage headers + license. Consumer-side yxml.cmake points
# INTERFACE_INCLUDE_DIRECTORIES at $_YXML_DIR/include.
#-----------------------------------------------------------------------------
cp "$SRC_DIR/yxml.h"   "$STAGE/include/yxml.h"
cp "$SRC_DIR/COPYING"  "$STAGE/COPYING"

# Sanity-check the static lib exists and is non-empty.
case "$TARGET_PLATFORM" in
    windows-x86_64) _LIB="$STAGE/lib/yxml.lib" ;;
    *)              _LIB="$STAGE/lib/libyxml.a" ;;
esac
[ -s "$_LIB" ]                  || { echo "missing/empty $_LIB" >&2; exit 1; }
[ -s "$STAGE/include/yxml.h" ]  || { echo "missing yxml.h"      >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "yxml @${SHORT} ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
