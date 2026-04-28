#!/bin/bash
# Builds imgui (ocornut/imgui) for $TARGET_PLATFORM. We compile the
# 5 platform-independent core .cpp files into libimgui_core.a, and
# ship the backend SOURCES (imgui_impl_glfw.cpp, imgui_impl_wgpu.cpp)
# so the yetty consumer can compile them with its specific
# WEBGPU_BACKEND / GLFW / Cocoa flags. Same idea libpng's prebuilt
# ships zlib-linked .a but keeps zlib symbols unresolved — backends are
# inherently target-flag-specific, prebuilding them per-host doesn't
# match yetty's 12-axis flag matrix.
#
# Output tarball layout (consumed by build-tools/cmake/libs/imgui.cmake):
#   lib/libimgui_core.a          — the 5 core .cpp prebuilt
#   include/imgui.h              — public headers (imgui.h, imgui_internal.h,
#                                  imconfig.h, imstb_*.h)
#   src-backends/imgui_impl_glfw.{cpp,h}
#   src-backends/imgui_impl_wgpu.{cpp,h}

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
mkdir -p "$STAGE/lib" "$STAGE/include" "$STAGE/src-backends"

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
        tvos-arm64)  _IOS_SDK="appletvsimulator"; _IOS_ARCH="arm64"
                     _MIN_FLAG="-mtvos-simulator-version-min=${TVOS_MIN}" ;;
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
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

CXXFLAGS="$CXXFLAGS_BASE $CXXFLAGS_EXTRA"

#-----------------------------------------------------------------------------
# Compile the 5 core sources.
#-----------------------------------------------------------------------------
CORE=(imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp)
OBJS=()
for _s in "${CORE[@]}"; do
    _o="$WORK_DIR/${_s%.cpp}-${TARGET_PLATFORM}.o"
    $CXX $CXXFLAGS -I"$SRC_DIR" -c "$SRC_DIR/$_s" -o "$_o"
    OBJS+=("$_o")
done
$AR rcs "$STAGE/lib/libimgui_core.a" "${OBJS[@]}"

#-----------------------------------------------------------------------------
# Stage public headers + backend source files.
#-----------------------------------------------------------------------------
for _h in imgui.h imgui_internal.h imconfig.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h; do
    [ -f "$SRC_DIR/$_h" ] && cp "$SRC_DIR/$_h" "$STAGE/include/" || true
done
# Backend sources for yetty's consumer to compile against its own
# platform flags (WEBGPU_BACKEND etc.).
for _b in imgui_impl_glfw.cpp imgui_impl_glfw.h imgui_impl_wgpu.cpp imgui_impl_wgpu.h; do
    [ -f "$SRC_DIR/backends/$_b" ] && cp "$SRC_DIR/backends/$_b" "$STAGE/src-backends/" || true
done

[ -f "$STAGE/lib/libimgui_core.a" ]                 || { echo "missing libimgui_core.a"           >&2; exit 1; }
[ -f "$STAGE/include/imgui.h" ]                     || { echo "missing imgui.h"                   >&2; exit 1; }
[ -f "$STAGE/src-backends/imgui_impl_wgpu.cpp" ]    || { echo "missing imgui_impl_wgpu.cpp"        >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "imgui $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
{ tar -tzf "$TARBALL" | head -20; } || true
