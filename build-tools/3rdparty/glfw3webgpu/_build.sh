#!/bin/bash
# Builds glfw3webgpu (eliemichel/glfw3webgpu) for $TARGET_PLATFORM.
# It's a single .c that creates a WebGPU surface from a glfw window —
# no upstream tag, pin via commit SHA. Depends on glfw + Dawn webgpu.h
# at compile time, but produces an archive with unresolved refs (the
# yetty link wires both).
#
# Output tarball layout (consumed by build-tools/cmake/libs/glfw.cmake):
#   lib/libglfw3webgpu.a
#   include/glfw3webgpu.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
GLFW_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/glfw/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-glfw3webgpu-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"

URL="https://github.com/eliemichel/glfw3webgpu/archive/${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/glfw3webgpu-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/glfw3webgpu-${VERSION}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/glfw3webgpu-${TARGET_PLATFORM}-${VERSION}.tar.gz"

GLFW_TAR_URL="$URL_BASE/lib-glfw-${GLFW_VERSION}/glfw-${TARGET_PLATFORM}-${GLFW_VERSION}.tar.gz"
GLFW_TARBALL="$CACHE_DIR/glfw-${TARGET_PLATFORM}-${GLFW_VERSION}.tar.gz"
GLFW_PREFIX="$WORK_DIR/glfw-${TARGET_PLATFORM}-${GLFW_VERSION}"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

fetch() {
    local url="$1" cache="$2" descr="$3" lock="$4"
    if [ ! -f "$cache" ]; then
        local part="$cache.part.$$"
        (
            if command -v flock >/dev/null 2>&1; then flock -x 9; fi
            if [ ! -f "$cache" ]; then
                echo "==> downloading $descr"
                curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$part" "$url"
                mv "$part" "$cache"
            fi
        ) 9>"$CACHE_DIR/.$lock.lock"
        rm -f "$part"
    fi
}

fetch "$URL"          "$TARBALL_CACHE" "glfw3webgpu @${VERSION:0:8}" glfw3webgpu-source
fetch "$GLFW_TAR_URL" "$GLFW_TARBALL"  "glfw ${GLFW_VERSION} (${TARGET_PLATFORM})" glfw3webgpu-glfw

if [ ! -d "$SRC_DIR" ]; then
    mkdir -p "$WORK_DIR/.extract-$$"
    tar -C "$WORK_DIR/.extract-$$" -xzf "$TARBALL_CACHE"
    mv "$WORK_DIR/.extract-$$/glfw3webgpu-${VERSION}" "$SRC_DIR"
    rmdir "$WORK_DIR/.extract-$$"
fi
rm -rf "$GLFW_PREFIX"
mkdir -p "$GLFW_PREFIX"
tar -C "$GLFW_PREFIX" -xzf "$GLFW_TARBALL"

rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/include"

# Webgpu header: yetty's dawn fetch publishes webgpu.h at a known path
# inside the build dir, but we don't have that here. Grab Dawn's headers
# directly from the prebuilt Dawn release tarball.
# Simpler: write a stub webgpu.h that forward-declares what glfw3webgpu
# actually uses (WGPUSurface, WGPUInstance). The yetty link supplies the
# real types via Dawn's webgpu.h on the consumer side.
#
# Actually: glfw3webgpu's source #includes <webgpu/webgpu.h>. Without
# that header, compilation fails. To keep this producer self-contained,
# fetch the upstream webgpu-headers single-header release.
# webgpu-native/webgpu-headers has no release tags — pin to a recent
# commit SHA. The archive URL accepts SHAs as refs.
WEBGPU_HEADERS_SHA="dc16b3e531cf4f31be54236d1a3e988ba5f295a2"
WEBGPU_HEADERS_URL="https://github.com/webgpu-native/webgpu-headers/archive/${WEBGPU_HEADERS_SHA}.tar.gz"
WEBGPU_HEADERS_TARBALL="$CACHE_DIR/webgpu-headers-${WEBGPU_HEADERS_SHA}.tar.gz"
fetch "$WEBGPU_HEADERS_URL" "$WEBGPU_HEADERS_TARBALL" "webgpu-headers @${WEBGPU_HEADERS_SHA:0:8}" glfw3webgpu-wh
WEBGPU_HEADERS_DIR="$WORK_DIR/webgpu-headers-${WEBGPU_HEADERS_SHA}"
rm -rf "$WEBGPU_HEADERS_DIR"
tar -C "$WORK_DIR" -xzf "$WEBGPU_HEADERS_TARBALL"
# webgpu-headers has webgpu.h at the repo root, but glfw3webgpu.c
# includes <webgpu/webgpu.h>. Set up the expected dir structure.
mkdir -p "$WEBGPU_HEADERS_DIR/include/webgpu"
ln -sf "$WEBGPU_HEADERS_DIR/webgpu.h" "$WEBGPU_HEADERS_DIR/include/webgpu/webgpu.h"

CFLAGS_BASE="-O2 -fPIC -DNDEBUG -w -DGLFW_INCLUDE_NONE"
CC=cc
AR=ar
CFLAGS_EXTRA=""
PLATFORM_DEF=""

case "$TARGET_PLATFORM" in
linux-x86_64|linux-aarch64)
    # X11 only — Wayland headers aren't in the 3rdparty-linux-* nix
    # shells (see glfw producer for the same restriction).
    PLATFORM_DEF="-D_GLFW_X11 -DWEBGPU_BACKEND_DAWN"
    case "$TARGET_PLATFORM" in
        linux-x86_64) CC=gcc ;;
        linux-aarch64)
            : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
            CC="${CROSS_PREFIX}gcc"; AR="${CROSS_PREFIX}ar" ;;
    esac
    ;;
macos-x86_64|macos-arm64)
    PLATFORM_DEF="-D_GLFW_COCOA -DWEBGPU_BACKEND_DAWN"
    CC=clang
    case "$TARGET_PLATFORM" in
        macos-x86_64) CFLAGS_EXTRA="-arch x86_64" ;;
        macos-arm64)  CFLAGS_EXTRA="-arch arm64"  ;;
    esac
    # glfw3webgpu uses Objective-C on macOS to grab the CAMetalLayer.
    # Compile as Obj-C. NO ARC — upstream's source predates ARC and
    # uses implicit `id -> void *` casts (e.g. `fromMetalLayer.layer =
    # metal_layer`) that ARC rejects without `__bridge`. MRR mode lets
    # them through.
    CFLAGS_EXTRA="$CFLAGS_EXTRA -ObjC -fno-objc-arc"
    ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cl.exe + lib.exe.
    # MSYS2_ARG_CONV_EXCL='*' is applied locally on each cl/lib call
    # below so MSVC /flags pass through. Do NOT export it globally —
    # git-bash on windows-latest needs MSYS conversion for the curl
    # invocations that fetch source tarballs.
    PLATFORM_DEF="/D_GLFW_WIN32 /DWEBGPU_BACKEND_DAWN"
    CC=cl
    AR=lib
    CFLAGS_BASE="/nologo /O2 /MT /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /DGLFW_INCLUDE_NONE"
    CFLAGS_EXTRA=""
    ;;
*) echo "unsupported $TARGET_PLATFORM" >&2; exit 1 ;;
esac

CFLAGS="$CFLAGS_BASE $CFLAGS_EXTRA $PLATFORM_DEF"

# glfw3webgpu's source file is glfw3webgpu.c. On macos it's built as
# obj-c via the -ObjC flag above.
echo "==> compiling glfw3webgpu @${VERSION:0:8} for $TARGET_PLATFORM"
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    # cygpath -w converts /tmp/... -> C:\msys64\tmp\... so cl sees a real
    # Windows path (it would otherwise treat the leading / as a flag).
    # MSYS2_ARG_CONV_EXCL='*' applied per-call so MSVC /flags pass
    # through.
    _SRC_W=$(cygpath -w "$SRC_DIR/glfw3webgpu.c")
    _OBJ_W=$(cygpath -w "$WORK_DIR/glfw3webgpu-${TARGET_PLATFORM}.obj")
    _OUT_W=$(cygpath -w "$STAGE/lib/libglfw3webgpu.lib")
    _SRC_DIR_W=$(cygpath -w "$SRC_DIR")
    _GLFW_INC_W=$(cygpath -w "$GLFW_PREFIX/include")
    _WEBGPU_INC_W=$(cygpath -w "$WEBGPU_HEADERS_DIR/include")
    MSYS2_ARG_CONV_EXCL='*' \
        $CC $CFLAGS \
            "/I${_SRC_DIR_W}" \
            "/I${_GLFW_INC_W}" \
            "/I${_WEBGPU_INC_W}" \
            /c "$_SRC_W" \
            "/Fo${_OBJ_W}"
    MSYS2_ARG_CONV_EXCL='*' \
        $AR /nologo "/OUT:${_OUT_W}" "${_OBJ_W}"
else
    $CC $CFLAGS \
        -I"$SRC_DIR" \
        -I"$GLFW_PREFIX/include" \
        -I"$WEBGPU_HEADERS_DIR/include" \
        -c "$SRC_DIR/glfw3webgpu.c" \
        -o "$WORK_DIR/glfw3webgpu-${TARGET_PLATFORM}.o"
    $AR rcs "$STAGE/lib/libglfw3webgpu.a" "$WORK_DIR/glfw3webgpu-${TARGET_PLATFORM}.o"
fi
cp "$SRC_DIR/glfw3webgpu.h" "$STAGE/include/"

if [ ! -f "$STAGE/lib/libglfw3webgpu.a" ] && [ ! -f "$STAGE/lib/libglfw3webgpu.lib" ]; then
    echo "missing libglfw3webgpu.a / libglfw3webgpu.lib" >&2
    exit 1
fi
[ -f "$STAGE/include/glfw3webgpu.h" ] || { echo "missing glfw3webgpu.h" >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "glfw3webgpu @${VERSION:0:8} ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
