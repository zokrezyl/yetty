#!/bin/bash
# Builds OpenSSL 1.1.1w (via janbar/openssl-cmake) for $TARGET_PLATFORM
# and packages it as a tarball under $OUTPUT_DIR. Mirrors the from-source
# build that build-tools/cmake/openssl.cmake currently triggers on
# Android/wasm — extended here to every cross target so yetty's main
# configure can fetch a prebuilt static lib instead of rebuilding.
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
# truth for both upstream fetch (janbar/openssl-cmake tag) and tarball
# naming.
#
# Optional env:
#   WORK_DIR          default /tmp/yetty-3rdparty-openssl-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#                     holds the upstream tarball so multi-target builds
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

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-openssl-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# janbar/openssl-cmake uses GitHub-archive URLs. Tag name == version
# verbatim (e.g. `1.1.1w-20250419`).
OPENSSL_URL="https://github.com/janbar/openssl-cmake/archive/refs/tags/${VERSION}.tar.gz"
OPENSSL_TARBALL="$CACHE_DIR/openssl-cmake-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/openssl-cmake-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/openssl-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch (shared across targets) — flock so parallel target builds share
# a single download. Per-PID .part avoids clobbering on hosts without flock.
#-----------------------------------------------------------------------------
if [ ! -f "$OPENSSL_TARBALL" ]; then
    _part="$OPENSSL_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then
            flock -x 9
        fi
        if [ ! -f "$OPENSSL_TARBALL" ]; then
            echo "==> downloading janbar/openssl-cmake ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$OPENSSL_URL"
            mv "$_part" "$OPENSSL_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.openssl-download.lock"
    rm -f "$_part"
else
    echo "==> using cached openssl-cmake source: $OPENSSL_TARBALL"
fi

#-----------------------------------------------------------------------------
# Extract (out-of-source build, shared SRC_DIR safe across targets) +
# apply the LONG_INT patch from build-tools/cmake/openssl.cmake.
#
# When LONG_INT is empty (Emscripten + a few exotic targets), upstream's
# `if( HAVE_LONG_INT AND (${LONG_INT} EQUAL 8) )` crashes CMake. Quoting
# fixes it. The patch is idempotent — running twice is a no-op.
#-----------------------------------------------------------------------------
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$OPENSSL_TARBALL"
fi

# Use python3 (available in every 3rdparty-* nix shell) for the patch:
# str.replace doesn't interpret special chars, no shell/regex escaping
# headaches. Idempotent — running twice is a no-op.
python3 - "$SRC_DIR/CMakeLists.txt" <<'PYEOF'
import sys, pathlib
p = pathlib.Path(sys.argv[1])
old = "if( HAVE_LONG_INT AND (${LONG_INT} EQUAL 8) )"
new = "if( HAVE_LONG_INT AND (\"${LONG_INT}\" EQUAL 8) )"
c = p.read_text()
if old in c:
    p.write_text(c.replace(old, new))
    print(f"==> patched LONG_INT quoting in {p.name}")
elif new in c:
    print(f"==> LONG_INT patch already applied in {p.name}")
else:
    print(f"WARNING: LONG_INT patch site not found in {p.name}", file=sys.stderr)
PYEOF

rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

#-----------------------------------------------------------------------------
# Per-platform CMake args. The upstream build is plain CMake — much
# simpler than dav1d's meson cross files.
#-----------------------------------------------------------------------------
CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DWITH_APPS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
EMCMAKE_PREFIX=""    # "emcmake" prefix for webasm; empty otherwise

case "$TARGET_PLATFORM" in

linux-x86_64)
    : # native gcc on host
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
        "-DCMAKE_AR=$(command -v ${CROSS_PREFIX}ar 2>/dev/null || echo /usr/bin/ar)"
        "-DCMAKE_RANLIB=$(command -v ${CROSS_PREFIX}ranlib 2>/dev/null || echo /usr/bin/ranlib)"
    )
    ;;

macos-x86_64)
    CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64")
    ;;

macos-arm64)
    CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")
    ;;

ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64)
    # Same nix-on-macOS xcrun trap as openh264 / dav1d — see those scripts
    # for the full explanation. /usr/bin first on PATH, unset nix apple-sdk.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"

    : "${IOS_MIN:=15.0}"
    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)
            _IOS_SDK="iphoneos";         _IOS_ARCH="arm64"
            _CMAKE_SYS="iOS";  _CMAKE_DEPL="$IOS_MIN"
            ;;
        ios-x86_64)
            _IOS_SDK="iphonesimulator";  _IOS_ARCH="x86_64"
            _CMAKE_SYS="iOS";  _CMAKE_DEPL="$IOS_MIN"
            ;;
        tvos-x86_64)
            _IOS_SDK="appletvsimulator"; _IOS_ARCH="x86_64"
            _CMAKE_SYS="tvOS"; _CMAKE_DEPL="$TVOS_MIN"
            ;;
        tvos-arm64)
            _IOS_SDK="appletvos"       ; _IOS_ARCH="arm64"
            _CMAKE_SYS="tvOS"; _CMAKE_DEPL="$TVOS_MIN"
            ;;
    esac
    _IOS_SYSROOT="$(/usr/bin/xcrun --sdk "$_IOS_SDK" --show-sdk-path)"
    [ -d "$_IOS_SYSROOT" ] || { echo "missing SDK: $_IOS_SYSROOT" >&2; exit 1; }

    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=$_CMAKE_SYS"
        "-DCMAKE_OSX_ARCHITECTURES=$_IOS_ARCH"
        "-DCMAKE_OSX_SYSROOT=$_IOS_SYSROOT"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=$_CMAKE_DEPL"
        "-DCMAKE_C_COMPILER=/usr/bin/clang"
        "-DCMAKE_CXX_COMPILER=/usr/bin/clang++"
    )
    ;;

android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _ANDROID_ABI=arm64-v8a ;;
        android-x86_64)    _ANDROID_ABI=x86_64    ;;
    esac
    CMAKE_ARGS+=(
        "-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake"
        "-DANDROID_ABI=${_ANDROID_ABI}"
        "-DANDROID_PLATFORM=android-${ANDROID_API}"
        "-DANDROID_NDK=${ANDROID_NDK_HOME}"
    )
    ;;

webasm)
    command -v emcmake >/dev/null 2>&1 || {
        echo "error: emcmake not found — source the .#3rdparty-webasm shell" >&2
        exit 1
    }
    EMCMAKE_PREFIX="emcmake"
    ;;

windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. Use Ninja so
    # the static lib lands at $INSTALL_DIR/lib/{ssl,crypto}.lib.
    CMAKE_ARGS+=("-DCMAKE_SYSTEM_NAME=Windows")
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Configure + build + install
#-----------------------------------------------------------------------------
echo "==> configuring openssl for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"

echo "==> building (-j${NCPU})"
cmake --build "$BUILD_DIR" -j"$NCPU"

echo "==> installing"
cmake --install "$BUILD_DIR"

#-----------------------------------------------------------------------------
# Verify install layout. janbar/openssl-cmake names the static libs
# crypto/ssl on unix-like targets, libcrypto/libssl on Windows; some
# multi-arch hosts install to lib64/. Normalise both into lib/ in stage.
#-----------------------------------------------------------------------------
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    _LIBS=("libssl.lib" "libcrypto.lib")
else
    _LIBS=("libssl.a" "libcrypto.a")
fi

mkdir -p "$STAGE/lib"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        cp -a "$INSTALL_DIR/$_D/." "$STAGE/lib/"
    fi
done

# Normalise versioned suffixes — janbar/openssl-cmake names the static
# archives `libssl_1_1.a` / `libcrypto_1_1.a` on Android (and possibly
# others), but plain `libssl.a` on linux-x86_64. Rename the suffixed
# variants to the bare names so consumer-side filename is stable.
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    _EXT=lib
else
    _EXT=a
fi
for _name in libssl libcrypto; do
    if [ ! -f "$STAGE/lib/${_name}.${_EXT}" ]; then
        for _suff in _1_1 -1_1 .1.1 -1_1-x64; do
            _src="$STAGE/lib/${_name}${_suff}.${_EXT}"
            if [ -f "$_src" ]; then
                mv "$_src" "$STAGE/lib/${_name}.${_EXT}"
                echo "==> normalised ${_name}${_suff}.${_EXT} -> ${_name}.${_EXT}"
                break
            fi
        done
    fi
done

for _LIB in "${_LIBS[@]}"; do
    if [ ! -f "$STAGE/lib/$_LIB" ]; then
        echo "missing library: $STAGE/lib/$_LIB" >&2
        echo "install tree:" >&2
        find "$INSTALL_DIR" -maxdepth 4 -print >&2 || true
        exit 1
    fi
done

if [ ! -f "$INSTALL_DIR/include/openssl/ssl.h" ]; then
    echo "missing headers: $INSTALL_DIR/include/openssl/" >&2
    exit 1
fi
cp -a "$INSTALL_DIR/include" "$STAGE/"

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "openssl $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 20 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,20p'
