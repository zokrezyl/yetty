#!/bin/bash
# Builds glfw (glfw/glfw) for $TARGET_PLATFORM via its upstream CMake.
# Desktop-only: yetty's mobile/web builds don't use glfw.
#
# Output tarball layout (consumed by build-tools/cmake/libs/glfw.cmake):
#   lib/libglfw3.a
#   include/GLFW/glfw3.h, glfw3native.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-glfw-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL="https://github.com/glfw/glfw/archive/refs/tags/${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/glfw-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/glfw-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/glfw-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.glfw-download.lock"
    rm -f "$_part"
fi

if [ ! -d "$SRC_DIR" ]; then tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"; fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"


CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    # Force `lib` (not `lib64` / `lib/<triple>-linux-gnu`) so the stage
    # step's hard-coded `$INSTALL_DIR/lib` lookup catches everything,
    # regardless of distro/cross-toolchain libdir conventions.
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=Release
    -DGLFW_BUILD_DOCS=OFF
    -DGLFW_BUILD_TESTS=OFF
    -DGLFW_BUILD_EXAMPLES=OFF
    -DGLFW_INSTALL=ON
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

case "$TARGET_PLATFORM" in
linux-x86_64|linux-aarch64|linux-riscv64)
    # Both X11 and Wayland enabled. GLFW picks the backend at runtime
    # (X11 if DISPLAY is set or XDG_SESSION_TYPE != "wayland", else
    # Wayland). The runner installs the X11 / wayland / xkbcommon dev
    # packages via apt before invoking this script.
    CMAKE_ARGS+=(
        -DGLFW_BUILD_X11=ON
        -DGLFW_BUILD_WAYLAND=ON
    )
    # Cross-compile flags only when host arch != target arch. On a native
    # GitHub ubuntu-24.04-arm runner targeting linux-aarch64, the host is
    # already aarch64 and the stock /usr/bin/gcc is the right compiler — no
    # cross prefix, no CMAKE_SYSTEM_NAME override needed (those would force
    # CMAKE to flip into cross mode and refuse to use the native pkg-config
    # results).
    _HOST_ARCH="$(uname -m)"
    if [ "$TARGET_PLATFORM" = "linux-aarch64" ] && [ "$_HOST_ARCH" != "aarch64" ]; then
        : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
        CMAKE_ARGS+=(
            "-DCMAKE_SYSTEM_NAME=Linux"
            "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
            "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        )
    elif [ "$TARGET_PLATFORM" = "linux-riscv64" ] && [ "$_HOST_ARCH" != "riscv64" ]; then
        : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
        CMAKE_ARGS+=(
            "-DCMAKE_SYSTEM_NAME=Linux"
            "-DCMAKE_SYSTEM_PROCESSOR=riscv64"
            "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        )
    fi
    ;;
macos-x86_64|macos-arm64)
    CMAKE_ARGS+=(
        -DGLFW_BUILD_X11=OFF
        -DGLFW_BUILD_WAYLAND=OFF
    )
    case "$TARGET_PLATFORM" in
        macos-x86_64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
        macos-arm64)  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")  ;;
    esac
    ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cmake auto-
    # detects cl.exe. GLFW upstream supports Win32 natively.
    CMAKE_ARGS+=(
        -DGLFW_BUILD_X11=OFF
        -DGLFW_BUILD_WAYLAND=OFF
    )
    ;;
*) echo "unsupported $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring glfw ${VERSION} for $TARGET_PLATFORM"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$NCPU"
cmake --install "$BUILD_DIR"

mkdir -p "$STAGE/lib" "$STAGE/include"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        find "$INSTALL_DIR/$_D" -maxdepth 1 \
            \( -name 'libglfw*.a' -o -name 'glfw3.lib' -o -name 'glfw*.lib' \) \
            -exec cp -a {} "$STAGE/lib/" \;
        # Stage upstream's exported cmake config + pkg-config so consumers
        # can pull INTERFACE_LINK_LIBRARIES (X11/Wayland/xkbcommon/pthread/
        # dl/m/rt) directly from glfw3Config.cmake instead of duplicating
        # platform deps in build-tools/cmake/libs/glfw.cmake.
        if [ -d "$INSTALL_DIR/$_D/cmake/glfw3" ]; then
            mkdir -p "$STAGE/lib/cmake/glfw3"
            cp -a "$INSTALL_DIR/$_D/cmake/glfw3/." "$STAGE/lib/cmake/glfw3/"
        fi
        if [ -d "$INSTALL_DIR/$_D/pkgconfig" ]; then
            mkdir -p "$STAGE/lib/pkgconfig"
            cp -a "$INSTALL_DIR/$_D/pkgconfig/." "$STAGE/lib/pkgconfig/"
        fi
    fi
done

# Some cmake configurations (notably cross-compile shells) install the
# package config under share/cmake/glfw3 instead of lib/cmake/glfw3 —
# pick that up as a fallback so the staged tarball is always usable by
# find_package(glfw3 CONFIG).
if [ ! -d "$STAGE/lib/cmake/glfw3" ] && [ -d "$INSTALL_DIR/share/cmake/glfw3" ]; then
    mkdir -p "$STAGE/lib/cmake/glfw3"
    cp -a "$INSTALL_DIR/share/cmake/glfw3/." "$STAGE/lib/cmake/glfw3/"
fi

# Sanity: warn loudly if we still don't have a glfw3Config.cmake. The
# consumer-side build-tools/yetty/libs/glfw.cmake has a fallback path
# that hand-builds the IMPORTED target, but the cmake config is the
# preferred shape and keeps platform deps in lockstep with upstream.
if [ ! -f "$STAGE/lib/cmake/glfw3/glfw3Config.cmake" ]; then
    echo "warning: glfw3Config.cmake not staged — consumer will fall back" >&2
    echo "         to a hand-built IMPORTED target." >&2
    echo "         INSTALL_DIR layout:" >&2
    find "$INSTALL_DIR" -type f \( -name '*Config.cmake' -o -name 'glfw3.pc' \) >&2 || true
fi

cp -a "$INSTALL_DIR/include/GLFW" "$STAGE/include/"

# Stage GLFW's PRIVATE headers + the generated Wayland protocol headers so
# yetty can compile a tiny helper TU against them. Reason: GLFW 3.4 does
# not expose `xdg_toplevel` (the per-window handle), `wl_seat`, or the
# last button serial via its public native API, but Wayland's
# `xdg_toplevel.move(seat, serial)` is the only protocol-correct way to
# move a CSD window. GLFW's own internal pointerHandleButton calls
# `xdg_toplevel_move(window->wl.xdg.toplevel, _glfw.wl.seat, serial)`
# (see src/wl_window.c:1565); we replicate that call from our window-
# manager. To do it we need the same `_GLFWwindow` / `_GLFWlibrary`
# struct layout the static lib was built with — hence "same headers as
# the lib", staged here next to libglfw3.a.
#
# This is desktop-Linux-only (Wayland backend); the macOS/Windows
# tarballs don't ship the private headers because no consumer needs them.
case "$TARGET_PLATFORM" in
linux-x86_64|linux-aarch64|linux-riscv64)
    mkdir -p "$STAGE/include-private"
    cp -a "$SRC_DIR/src/." "$STAGE/include-private/"
    # The generated client-protocol headers live in BUILD_DIR/src — wayland-
    # scanner emitted them during the configure/build phase. We need
    # `xdg-shell-client-protocol.h` so our helper can call
    # `xdg_toplevel_move()` (the function is a static inline in that header
    # that wraps wl_proxy_marshal).
    if [ -f "$BUILD_DIR/src/xdg-shell-client-protocol.h" ]; then
        cp -a "$BUILD_DIR/src/xdg-shell-client-protocol.h" \
            "$STAGE/include-private/"
    else
        echo "warning: generated xdg-shell-client-protocol.h not found in BUILD_DIR/src" >&2
    fi
    ;;
esac

if [ ! -f "$STAGE/lib/libglfw3.a" ] && [ ! -f "$STAGE/lib/glfw3.lib" ]; then
    echo "missing libglfw3.a / glfw3.lib in stage" >&2
    find "$INSTALL_DIR" >&2
    exit 1
fi
[ -f "$STAGE/include/GLFW/glfw3.h" ]      || { echo "missing GLFW/glfw3.h"          >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "glfw $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
