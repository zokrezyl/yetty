#!/bin/bash
# Builds imgui (ocornut/imgui) for $TARGET_PLATFORM. We compile the
# 5 platform-independent core .cpp files into libimgui_core.a. The
# tarball is pure imgui core — no GLFW / WebGPU backend sources, no
# X11. Consumers that need a platform/renderer backend (yetty's main
# desktop exec) bring their own.
#
# Output tarball layout (consumed by build-tools/cmake/libs/imgui.cmake):
#   lib/libimgui_core.a          — the 5 core .cpp prebuilt
#   include/imgui.h              — public headers (imgui.h, imgui_internal.h,
#                                  imconfig.h, imstb_*.h)

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-imgui-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL="https://github.com/ocornut/imgui/archive/refs/tags/v${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/imgui-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/imgui-${VERSION}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/imgui-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.imgui-download.lock"
    rm -f "$_part"
fi
if [ ! -d "$SRC_DIR" ]; then tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"; fi
rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/include"

#-----------------------------------------------------------------------------
# Per-platform compiler.
#-----------------------------------------------------------------------------
CXXFLAGS_BASE="-O2 -fPIC -DNDEBUG -std=c++17 -w"
CXX=c++
AR=ar
CXXFLAGS_EXTRA=""

case "$TARGET_PLATFORM" in
linux-x86_64) CXX=g++ ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CXX="${CROSS_PREFIX}g++"; AR="${CROSS_PREFIX}ar"
    ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CXX="${CROSS_PREFIX}g++"; AR="${CROSS_PREFIX}ar"
    ;;
macos-x86_64) CXX=clang++; CXXFLAGS_EXTRA="-arch x86_64" ;;
macos-arm64)  CXX=clang++; CXXFLAGS_EXTRA="-arch arm64"  ;;
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
    CXX=/usr/bin/clang++; AR=/usr/bin/ar
    CXXFLAGS_EXTRA="-arch $_IOS_ARCH -isysroot $_IOS_SYSROOT $_MIN_FLAG"
    ;;
android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _T="aarch64-linux-android" ;;
        android-x86_64)    _T="x86_64-linux-android"  ;;
    esac
    CXX="${_T}${ANDROID_API}-clang++"; AR="llvm-ar"
    ;;
webasm) CXX=em++; AR=emar ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cl.exe + lib.exe.
    # MSYS2_ARG_CONV_EXCL='*' is applied locally on each cl/lib call
    # below so MSVC /flags pass through. Do NOT export it globally —
    # git-bash on windows-latest needs MSYS conversion for the curl
    # invocation that fetches the imgui source tarball.
    CXX=cl
    AR=lib
    CXXFLAGS_BASE="/nologo /O2 /MT /EHsc /std:c++17 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG"
    CXXFLAGS_EXTRA=""
    ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

CXXFLAGS="$CXXFLAGS_BASE $CXXFLAGS_EXTRA"

#-----------------------------------------------------------------------------
# Compile the 5 core sources.
#-----------------------------------------------------------------------------
CORE=(imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp)
OBJS=()
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    # cygpath -w converts /tmp/... -> C:\msys64\tmp\... so cl sees a real
    # Windows path (it would otherwise treat the leading / as a flag).
    # MSYS2_ARG_CONV_EXCL='*' applied per-call so MSVC /flags pass
    # through.
    _SRC_DIR_W=$(cygpath -w "$SRC_DIR")
    for _s in "${CORE[@]}"; do
        _o="$WORK_DIR/${_s%.cpp}-${TARGET_PLATFORM}.obj"
        _o_w=$(cygpath -w "$_o")
        _src_w=$(cygpath -w "$SRC_DIR/$_s")
        MSYS2_ARG_CONV_EXCL='*' \
            $CXX $CXXFLAGS "/I${_SRC_DIR_W}" /c "$_src_w" "/Fo${_o_w}"
        OBJS+=("$_o")
    done
    _OUT_W=$(cygpath -w "$STAGE/lib/libimgui_core.lib")
    _OBJS_W=()
    for _o in "${OBJS[@]}"; do _OBJS_W+=("$(cygpath -w "$_o")"); done
    MSYS2_ARG_CONV_EXCL='*' \
        $AR /nologo "/OUT:${_OUT_W}" "${_OBJS_W[@]}"
else
    for _s in "${CORE[@]}"; do
        _o="$WORK_DIR/${_s%.cpp}-${TARGET_PLATFORM}.o"
        $CXX $CXXFLAGS -I"$SRC_DIR" -c "$SRC_DIR/$_s" -o "$_o"
        OBJS+=("$_o")
    done
    $AR rcs "$STAGE/lib/libimgui_core.a" "${OBJS[@]}"
fi

#-----------------------------------------------------------------------------
# Stage public headers only — pure imgui core, no GLFW/WebGPU backends.
#-----------------------------------------------------------------------------
for _h in imgui.h imgui_internal.h imconfig.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h; do
    [ -f "$SRC_DIR/$_h" ] && cp "$SRC_DIR/$_h" "$STAGE/include/" || true
done

if [ ! -f "$STAGE/lib/libimgui_core.a" ] && [ ! -f "$STAGE/lib/libimgui_core.lib" ]; then
    echo "missing libimgui_core.a / libimgui_core.lib" >&2
    exit 1
fi
[ -f "$STAGE/include/imgui.h" ]                     || { echo "missing imgui.h"                   >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "imgui $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
{ tar -tzf "$TARBALL" | head -20; } || true
