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
    # Wayland). The 3rdparty-linux-* nix shells in flake.nix carry the
    # wayland-protocols/wayland/libxkbcommon -dev pkgconfigs glfw probes
    # for; tearing the comment further would lie if either side moves.
    CMAKE_ARGS+=(
        -DGLFW_BUILD_X11=ON
        -DGLFW_BUILD_WAYLAND=ON
    )
    # nix's cmake doesn't auto-resolve X11 via NIX_CFLAGS_COMPILE for
    # find_package(X11). pkg-config sees them; pull the paths from
    # there and inject them as cache hints. The xorg deps come from
    # the 3rdparty-linux-* shells in flake.nix.
    #
    # On linux-aarch64 the cross shell ships an
    # `aarch64-unknown-linux-gnu-pkg-config` wrapper that knows about
    # the cross PKG_CONFIG_PATH; the system /usr/bin/pkg-config (which
    # `command -v pkg-config` finds first on Ubuntu CI) does NOT, so
    # prefer the cross-prefixed wrapper when present.
    _PC=pkg-config
    if [ "$TARGET_PLATFORM" = "linux-aarch64" ] && \
       command -v "${CROSS_PREFIX:-aarch64-unknown-linux-gnu-}pkg-config" >/dev/null 2>&1; then
        _PC="${CROSS_PREFIX:-aarch64-unknown-linux-gnu-}pkg-config"
    elif [ "$TARGET_PLATFORM" = "linux-riscv64" ] && \
         command -v "${CROSS_PREFIX:-riscv64-unknown-linux-gnu-}pkg-config" >/dev/null 2>&1; then
        _PC="${CROSS_PREFIX:-riscv64-unknown-linux-gnu-}pkg-config"
    fi
    if command -v "$_PC" >/dev/null 2>&1 && "$_PC" --exists x11; then
        _X11_INC="$($_PC --variable=includedir x11)"
        _X11_LIBDIR="$($_PC --variable=libdir x11)"
        CMAKE_ARGS+=(
            "-DX11_X11_INCLUDE_PATH=$_X11_INC"
            "-DX11_X11_LIB=$_X11_LIBDIR/libX11.so"
            "-DX11_INCLUDE_DIR=$_X11_INC"
        )
        # Other xorg components glfw probes for. Only set if pkg-config
        # knows about them; otherwise let cmake try its defaults.
        for _xorg in Xrandr Xinerama Xcursor Xi Xext; do
            _pc_name="$(echo "$_xorg" | tr '[:upper:]' '[:lower:]')"
            if "$_PC" --exists "$_pc_name"; then
                _inc="$($_PC --variable=includedir "$_pc_name")"
                _libdir="$($_PC --variable=libdir "$_pc_name")"
                CMAKE_ARGS+=(
                    "-DX11_${_xorg}_INCLUDE_PATH=$_inc"
                    "-DX11_${_xorg}_LIB=$_libdir/lib${_xorg}.so"
                )
            fi
        done
        # xorgproto isn't a pkg-config package, but its include dir is
        # injected via NIX_CFLAGS_COMPILE in the 3rdparty nix shell.
        # Search NIX_CFLAGS_COMPILE for an xorgproto path so glfw's
        # find_path(X11_Xkb_INCLUDE_PATH X11/extensions/XKB.h ...) hits.
        # NB: nix-store dirs are <hash>-xorgproto-<ver>/, so the hash
        # prefix breaks `*/xorgproto*` (which expects xorgproto right
        # after a slash). Use `*xorgproto*` to skip the hash.
        # Collect all xorg-related include paths from NIX_CFLAGS_COMPILE
        # so cmake's FindX11 can find every X11/extensions/*.h header
        # glfw probes (XKB, Xshape, Xrender, ...).
        _XORG_INCS=""
        for _f in ${NIX_CFLAGS_COMPILE:-}; do
            case "$_f" in
                *xorgproto*/include|*libx11*/include|*libxext*/include|*libxrandr*/include|*libxcursor*/include|*libxi*/include|*libxinerama*/include|*libxrender*/include|*libxfixes*/include)
                    if [ -z "$_XORG_INCS" ]; then
                        _XORG_INCS="$_f"
                    else
                        _XORG_INCS="$_XORG_INCS;$_f"
                    fi
                    case "$_f" in
                        *xorgproto*/include)
                            # Several X11_*_INCLUDE_PATH cmake cache vars
                            # all resolve to the same xorgproto dir —
                            # set the common ones explicitly so cmake
                            # doesn't have to scan paths.
                            CMAKE_ARGS+=(
                                "-DX11_Xkb_INCLUDE_PATH=$_f"
                                "-DX11_Xshape_INCLUDE_PATH=$_f"
                                "-DX11_Xrender_INCLUDE_PATH=$_f"
                                "-DX11_Xfixes_INCLUDE_PATH=$_f"
                            )
                            ;;
                    esac
                    ;;
            esac
        done
        if [ -n "$_XORG_INCS" ]; then
            CMAKE_ARGS+=("-DCMAKE_INCLUDE_PATH=$_XORG_INCS")
        fi
    fi
    if [ "$TARGET_PLATFORM" = "linux-aarch64" ]; then
        : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
        CMAKE_ARGS+=(
            "-DCMAKE_SYSTEM_NAME=Linux"
            "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
            "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        )
    elif [ "$TARGET_PLATFORM" = "linux-riscv64" ]; then
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
