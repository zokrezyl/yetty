#!/bin/bash
# Builds dav1d (videolan/dav1d) for $TARGET_PLATFORM and packages it as a
# tarball under $OUTPUT_DIR. Mirrors the per-platform meson invocations in
# build-tools/cmake/Dav1d.cmake — produces an installable bundle (lib/ +
# include/) that 3rdparty-fetch.cmake can drop into the build tree.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 | tvos-x86_64 |
#                     webasm | windows-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Version is read from this directory's `version` file — single source of
# truth for both upstream fetch and tarball naming.
#
# Optional env:
#   WORK_DIR          default /tmp/yetty-3rdparty-dav1d-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#                     holds the dav1d source tarball so multi-target builds
#                     share a single download
#   ANDROID_API       default 26 (matches yetty Android build)
#   IOS_MIN           default 15.0
#   TVOS_MIN          default 17.0
#   CROSS_PREFIX      default aarch64-unknown-linux-gnu- (linux-aarch64)

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-dav1d-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# VideoLAN GitLab archive endpoint — top-level dir is `dav1d-<VER>`.
DAV1D_URL="https://code.videolan.org/videolan/dav1d/-/archive/${VERSION}/dav1d-${VERSION}.tar.gz"
DAV1D_TARBALL="$CACHE_DIR/dav1d-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/dav1d-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/dav1d-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch (shared across targets) — flock so parallel target builds share
# a single download. Per-PID .part avoids clobbering on hosts without flock.
#-----------------------------------------------------------------------------
if [ ! -f "$DAV1D_TARBALL" ]; then
    _part="$DAV1D_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then
            flock -x 9
        fi
        if [ ! -f "$DAV1D_TARBALL" ]; then
            echo "==> downloading dav1d ${VERSION}"
            # code.videolan.org sporadically returns 5xx — generous retries.
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$DAV1D_URL"
            mv "$_part" "$DAV1D_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.dav1d-download.lock"
    rm -f "$_part"
else
    echo "==> using cached dav1d source: $DAV1D_TARBALL"
fi

#-----------------------------------------------------------------------------
# Extract — meson is out-of-source so a single shared SRC_DIR is fine.
#-----------------------------------------------------------------------------
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$DAV1D_TARBALL"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

#-----------------------------------------------------------------------------
# Per-platform meson configure: cross-files where needed, asm flags, etc.
# Mirrors the cmake recipe's case structure 1:1.
#-----------------------------------------------------------------------------
CROSS_FILE=""
CROSS_FLAG=""           # "--cross-file <f>" or "--native-file <f>" or empty
MESON_EXTRA_ARGS=()
POST_INSTALL_FIX=""     # iOS libtool merge (see bottom)

case "$TARGET_PLATFORM" in

linux-x86_64)
    : # native
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CROSS_FILE="$WORK_DIR/cross-linux-aarch64.ini"
    cat > "$CROSS_FILE" <<CROSS
[binaries]
c = '${CROSS_PREFIX}gcc'
cpp = '${CROSS_PREFIX}g++'
ar = '${CROSS_PREFIX}ar'
strip = '${CROSS_PREFIX}strip'
pkg-config = '${CROSS_PREFIX}pkg-config'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
CROSS
    CROSS_FLAG="--cross-file $CROSS_FILE"
    ;;

linux-riscv64)
    # dav1d has no RISC-V asm backend (only x86 + ARM); -Denable_asm=false
    # forces the pure-C build.
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CROSS_FILE="$WORK_DIR/cross-linux-riscv64.ini"
    cat > "$CROSS_FILE" <<CROSS
[binaries]
c = '${CROSS_PREFIX}gcc'
cpp = '${CROSS_PREFIX}g++'
ar = '${CROSS_PREFIX}ar'
strip = '${CROSS_PREFIX}strip'
pkg-config = '${CROSS_PREFIX}pkg-config'

[host_machine]
system = 'linux'
cpu_family = 'riscv64'
cpu = 'riscv64'
endian = 'little'
CROSS
    CROSS_FLAG="--cross-file $CROSS_FILE"
    MESON_EXTRA_ARGS+=("-Denable_asm=false")
    ;;

macos-x86_64)
    MESON_EXTRA_ARGS+=(
        "-Dc_args=-arch x86_64"
        "-Dcpp_args=-arch x86_64"
        "-Dc_link_args=-arch x86_64"
        "-Dcpp_link_args=-arch x86_64"
    )
    ;;

macos-arm64)
    MESON_EXTRA_ARGS+=(
        "-Dc_args=-arch arm64"
        "-Dcpp_args=-arch arm64"
        "-Dc_link_args=-arch arm64"
        "-Dcpp_link_args=-arch arm64"
    )
    ;;

android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a)
            ANDROID_TRIPLE=aarch64-linux-android
            DAV1D_CPU_FAMILY=aarch64; DAV1D_CPU=aarch64
            ;;
        android-x86_64)
            ANDROID_TRIPLE=x86_64-linux-android
            DAV1D_CPU_FAMILY=x86_64;  DAV1D_CPU=x86_64
            ;;
    esac
    ANDROID_CC="${ANDROID_TRIPLE}${ANDROID_API}-clang"
    ANDROID_CXX="${ANDROID_TRIPLE}${ANDROID_API}-clang++"
    command -v "$ANDROID_CC" >/dev/null 2>&1 || {
        echo "error: $ANDROID_CC not on PATH (NDK shellHook)" >&2
        exit 1
    }
    CROSS_FILE="$WORK_DIR/cross-android.ini"
    cat > "$CROSS_FILE" <<CROSS
[binaries]
c = '${ANDROID_CC}'
cpp = '${ANDROID_CXX}'
ar = 'llvm-ar'
strip = 'llvm-strip'
ranlib = 'llvm-ranlib'

[built-in options]
c_args = ['-DANDROID', '-fPIC']
cpp_args = ['-DANDROID', '-fPIC']

[host_machine]
system = 'android'
cpu_family = '${DAV1D_CPU_FAMILY}'
cpu = '${DAV1D_CPU}'
endian = 'little'
CROSS
    CROSS_FLAG="--cross-file $CROSS_FILE"
    ;;

ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64)
    # Same nix-on-macOS xcrun trap as openh264 — see that script for full
    # explanation. /usr/bin must be first on PATH for any bare-`xcrun`
    # shell-out (meson does this internally for SDK detection).
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"

    : "${IOS_MIN:=15.0}"
    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)
            _IOS_SDK="iphoneos"
            _IOS_ARCH="arm64"
            DAV1D_CPU_FAMILY=aarch64; DAV1D_CPU=aarch64
            _MIN_FLAG="-miphoneos-version-min=${IOS_MIN}"
            ;;
        ios-x86_64)
            _IOS_SDK="iphonesimulator"
            _IOS_ARCH="x86_64"
            DAV1D_CPU_FAMILY=x86_64;  DAV1D_CPU=x86_64
            _MIN_FLAG="-mios-simulator-version-min=${IOS_MIN}"
            ;;
        tvos-x86_64)
            _IOS_SDK="appletvsimulator"
            _IOS_ARCH="x86_64"
            DAV1D_CPU_FAMILY=x86_64;  DAV1D_CPU=x86_64
            _MIN_FLAG="-mtvos-simulator-version-min=${TVOS_MIN}"
            ;;
        tvos-arm64)
            _IOS_SDK="appletvsimulator"
            _IOS_ARCH="arm64"
            DAV1D_CPU_FAMILY=aarch64; DAV1D_CPU=aarch64
            _MIN_FLAG="-mtvos-version-min=${TVOS_MIN}"
            ;;
    esac
    _IOS_SYSROOT="$(/usr/bin/xcrun --sdk "$_IOS_SDK" --show-sdk-path)"
    [ -d "$_IOS_SYSROOT" ] || { echo "missing iOS SDK: $_IOS_SYSROOT" >&2; exit 1; }

    CROSS_FILE="$WORK_DIR/cross-ios.ini"
    cat > "$CROSS_FILE" <<CROSS
[binaries]
c = '/usr/bin/clang'
cpp = '/usr/bin/clang++'
ar = '/usr/bin/ar'
strip = '/usr/bin/strip'

[built-in options]
c_args = ['-arch', '${_IOS_ARCH}', '-isysroot', '${_IOS_SYSROOT}', '${_MIN_FLAG}']
c_link_args = ['-arch', '${_IOS_ARCH}', '-isysroot', '${_IOS_SYSROOT}']
cpp_args = ['-arch', '${_IOS_ARCH}', '-isysroot', '${_IOS_SYSROOT}', '${_MIN_FLAG}']
cpp_link_args = ['-arch', '${_IOS_ARCH}', '-isysroot', '${_IOS_SYSROOT}']

[host_machine]
system = 'darwin'
cpu_family = '${DAV1D_CPU_FAMILY}'
cpu = '${DAV1D_CPU}'
endian = 'little'
CROSS
    CROSS_FLAG="--cross-file $CROSS_FILE"
    # iOS NASM cross is fragile — disable asm. Matches Dav1d.cmake.
    MESON_EXTRA_ARGS+=("-Denable_asm=false")
    # iOS-only: meson uses GNU ar which produces archives Xcode rejects.
    # Re-merge .o files into one libdav1d.a via Apple libtool after install.
    POST_INSTALL_FIX="ios-libtool-merge"
    ;;

webasm)
    command -v emcc >/dev/null 2>&1 || {
        echo "error: emcc not found — source the .#3rdparty-webasm shell" >&2
        exit 1
    }
    CROSS_FILE="$WORK_DIR/cross-webasm.ini"
    cat > "$CROSS_FILE" <<CROSS
[binaries]
c = 'emcc'
cpp = 'em++'
ar = 'emar'
strip = 'emstrip'

[built-in options]
c_args = ['-fPIC', '-pthread']
c_link_args = ['-pthread']

[host_machine]
system = 'emscripten'
cpu_family = 'wasm32'
cpu = 'wasm32'
endian = 'little'
CROSS
    CROSS_FLAG="--cross-file $CROSS_FILE"
    MESON_EXTRA_ARGS+=("-Denable_asm=false")
    ;;

windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. Every lib that
    # links into yetty.exe needs MSVC ABI (qemu is the lone mingw build).
    #
    # Git Bash on the runner / dev box prepends /usr/bin to PATH for child
    # processes — and `/usr/bin/link.exe` is GNU coreutils' `link`. meson's
    # MSVC detection runs `where link` and trips on it:
    #   ERROR: Found GNU link.exe instead of MSVC link.exe in C:\Program Files\Git\usr\bin\link.EXE
    # Push MSVC's Hostx64\x64 (where MS link.exe lives) to the front so it
    # wins. vcvarsall sets $VCToolsInstallDir to the active MSVC version.
    if [ -n "${VCToolsInstallDir:-}" ]; then
        _MSVC_BIN="$(cygpath -u "$VCToolsInstallDir" 2>/dev/null)bin/Hostx64/x64"
        if [ -d "$_MSVC_BIN" ]; then
            export PATH="$_MSVC_BIN:$PATH"
        fi
    fi
    CROSS_FILE="$WORK_DIR/native-windows.ini"
    cat > "$CROSS_FILE" <<CROSS
[binaries]
c = 'cl'
cpp = 'cl'
ar = 'lib'

[built-in options]
c_args = []
cpp_args = []
CROSS
    CROSS_FLAG="--native-file $CROSS_FILE"
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Configure + build + install
#-----------------------------------------------------------------------------
echo "==> configuring dav1d for $TARGET_PLATFORM"
# shellcheck disable=SC2086     # CROSS_FLAG is "<flag> <file>" or empty
meson setup "$BUILD_DIR" "$SRC_DIR" \
    --prefix="$INSTALL_DIR" \
    --libdir=lib \
    --default-library=static \
    --buildtype=release \
    -Denable_tools=false \
    -Denable_tests=false \
    -Denable_examples=false \
    "${MESON_EXTRA_ARGS[@]}" \
    $CROSS_FLAG

echo "==> building (-j${NCPU})"
ninja -C "$BUILD_DIR" -j"$NCPU"

echo "==> installing"
ninja -C "$BUILD_DIR" install

#-----------------------------------------------------------------------------
# iOS: re-merge object files via Apple libtool.
# meson uses GNU ar (`llvm-ar` on darwin nix) which produces archives that
# Xcode's linker rejects with `malformed archive`. The workaround in the
# original Dav1d.cmake — replicated here.
#-----------------------------------------------------------------------------
if [ "$POST_INSTALL_FIX" = "ios-libtool-merge" ]; then
    echo "==> ios libtool merge"
    OUT_LIB="$INSTALL_DIR/lib/libdav1d.a"
    /usr/bin/xcrun libtool -static -o "$INSTALL_DIR/lib/libdav1d_fixed.a" \
        "$BUILD_DIR/src/libdav1d.a.p"/*.o \
        "$BUILD_DIR/src/libdav1d_bitdepth_8.a.p"/*.o \
        "$BUILD_DIR/src/libdav1d_bitdepth_16.a.p"/*.o
    mv "$INSTALL_DIR/lib/libdav1d_fixed.a" "$OUT_LIB"
fi

#-----------------------------------------------------------------------------
# Verify install layout
#-----------------------------------------------------------------------------
case "$TARGET_PLATFORM" in
    windows-x86_64) _LIB="$INSTALL_DIR/lib/dav1d.lib"  ;;
    *)              _LIB="$INSTALL_DIR/lib/libdav1d.a" ;;
esac
# meson on windows also writes libdav1d.a in some configurations — accept either.
if [ ! -f "$_LIB" ] && [ -f "$INSTALL_DIR/lib/libdav1d.a" ]; then
    _LIB="$INSTALL_DIR/lib/libdav1d.a"
fi
if [ ! -f "$_LIB" ]; then
    echo "missing library: $_LIB" >&2
    echo "install tree:" >&2
    find "$INSTALL_DIR" -maxdepth 4 -print >&2 || true
    exit 1
fi
if [ ! -f "$INSTALL_DIR/include/dav1d/dav1d.h" ]; then
    echo "missing headers: $INSTALL_DIR/include/dav1d/" >&2
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
echo "dav1d $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 20 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,20p'
