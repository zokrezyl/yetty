#!/bin/bash
# Builds libssh2 (libssh2/libssh2) for $TARGET_PLATFORM via its upstream
# CMake. Same per-platform handling pattern as libuv.
#
# OpenSSL backend: we link against the upstream openssl 4.x prebuilt
# tarball published by build-3rdparty-openssl.yml. To keep the
# producer self-contained, this script downloads that tarball at build
# time (instead of requiring the consumer-side fetch to have run first).
# Same model the consumer libssh2.cmake uses at yetty-build time —
# the difference is we resolve openssl HERE so the resulting libssh2
# archive carries no external configure-time path baked in.
#
# windows-x86_64 is intentionally absent — see build.sh for the why.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 |
#                     webasm
#   OUTPUT_DIR        where the tarball is written
#
# Optional env:
#   YETTY_3RDPARTY_URL_BASE  default https://github.com/zokrezyl/yetty/releases/download
#   OPENSSL_VERSION_OVERRIDE  pin a different openssl version
#                              (default: read from
#                               build-tools/3rdparty/openssl/version)
#
# Output tarball layout (consumed by build-tools/cmake/libs/libssh2.cmake):
#   lib/libssh2.a
#   include/libssh2.h
#   include/libssh2_publickey.h
#   include/libssh2_sftp.h

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

# OpenSSL version to link against. Read from the openssl 3rdparty
# dir unless overridden — that pins libssh2's TLS backend to the same
# version yetty itself uses.
OSSL_VERSION_FILE="$REPO_ROOT/build-tools/3rdparty/openssl/version"
: "${OPENSSL_VERSION_OVERRIDE:=}"
if [ -n "$OPENSSL_VERSION_OVERRIDE" ]; then
    OSSL_VERSION="$OPENSSL_VERSION_OVERRIDE"
else
    [ -f "$OSSL_VERSION_FILE" ] || { echo "missing $OSSL_VERSION_FILE" >&2; exit 1; }
    OSSL_VERSION="$(tr -d '[:space:]' < "$OSSL_VERSION_FILE")"
fi
[ -n "$OSSL_VERSION" ] || { echo "openssl version unresolved" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libssh2-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"

LIBSSH2_URL="https://github.com/libssh2/libssh2/archive/refs/tags/libssh2-${VERSION}.tar.gz"
LIBSSH2_TARBALL="$CACHE_DIR/libssh2-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/libssh2-libssh2-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libssh2-${TARGET_PLATFORM}-${VERSION}.tar.gz"

# Where we'll extract the prebuilt openssl for cmake to find.
OSSL_TAR_URL="$URL_BASE/lib-openssl-${OSSL_VERSION}/openssl-${TARGET_PLATFORM}-${OSSL_VERSION}.tar.gz"
OSSL_TARBALL="$CACHE_DIR/openssl-${TARGET_PLATFORM}-${OSSL_VERSION}.tar.gz"
OSSL_PREFIX="$WORK_DIR/openssl-${TARGET_PLATFORM}-${OSSL_VERSION}"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch helper (flock-protected, retry-friendly).
#-----------------------------------------------------------------------------
fetch() {
    local url="$1" cache="$2" descr="$3" lock="$4"
    if [ ! -f "$cache" ]; then
        local part="$cache.part.$$"
        (
            if command -v flock >/dev/null 2>&1; then flock -x 9; fi
            if [ ! -f "$cache" ]; then
                echo "==> downloading $descr"
                curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                    -o "$part" "$url"
                mv "$part" "$cache"
            fi
        ) 9>"$CACHE_DIR/.$lock.lock"
        rm -f "$part"
    fi
}

#-----------------------------------------------------------------------------
# Fetch libssh2 source + prebuilt openssl tarball.
#-----------------------------------------------------------------------------
fetch "$LIBSSH2_URL" "$LIBSSH2_TARBALL" "libssh2 ${VERSION}"               libssh2-source
fetch "$OSSL_TAR_URL" "$OSSL_TARBALL"   "openssl ${OSSL_VERSION} (${TARGET_PLATFORM}) — libssh2 TLS backend" libssh2-openssl

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting libssh2 -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$LIBSSH2_TARBALL"
fi
echo "==> extracting prebuilt openssl -> $OSSL_PREFIX"
rm -rf "$OSSL_PREFIX"
mkdir -p "$OSSL_PREFIX"
tar -C "$OSSL_PREFIX" -xzf "$OSSL_TARBALL"

# Sanity-check the prebuilt openssl tarball — Unix ships libssl.a /
# libcrypto.a, MSVC ships libssl.lib / libcrypto.lib.
OSSL_SSL=""; OSSL_CRYPTO=""
for _candidate in libssl.a libssl.lib; do
    if [ -f "$OSSL_PREFIX/lib/$_candidate" ]; then
        OSSL_SSL="$OSSL_PREFIX/lib/$_candidate"
        break
    fi
done
for _candidate in libcrypto.a libcrypto.lib; do
    if [ -f "$OSSL_PREFIX/lib/$_candidate" ]; then
        OSSL_CRYPTO="$OSSL_PREFIX/lib/$_candidate"
        break
    fi
done
[ -n "$OSSL_SSL" ]    || { echo "missing libssl in $OSSL_PREFIX/lib/ — openssl tarball layout?" >&2; ls -la "$OSSL_PREFIX/lib/" >&2; exit 1; }
[ -n "$OSSL_CRYPTO" ] || { echo "missing libcrypto in $OSSL_PREFIX/lib/ — openssl tarball layout?" >&2; ls -la "$OSSL_PREFIX/lib/" >&2; exit 1; }
[ -d "$OSSL_PREFIX/include/openssl" ] || { echo "missing $OSSL_PREFIX/include/openssl/" >&2; exit 1; }

rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

#-----------------------------------------------------------------------------
# Per-platform CMake args.
#-----------------------------------------------------------------------------
CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_STATIC_LIBS=ON
    -DBUILD_EXAMPLES=OFF
    -DBUILD_TESTING=OFF
    # libssh2 1.11.x cmake var name is CRYPTO_BACKEND.
    -DCRYPTO_BACKEND=OpenSSL
    # Point cmake at our prebuilt openssl (find_package(OpenSSL) honors
    # OPENSSL_ROOT_DIR + the *_LIBRARY / *_INCLUDE_DIR cache vars).
    -DOPENSSL_ROOT_DIR="$OSSL_PREFIX"
    -DOPENSSL_USE_STATIC_LIBS=ON
    -DOPENSSL_INCLUDE_DIR="$OSSL_PREFIX/include"
    -DOPENSSL_SSL_LIBRARY="$OSSL_SSL"
    -DOPENSSL_CRYPTO_LIBRARY="$OSSL_CRYPTO"
    # libssh2's zlib compression is off — yetty links zlib downstream
    # via consumer wiring; folding it into libssh2 here would conflict
    # with that. Same as the from-source build.
    -DENABLE_ZLIB_COMPRESSION=OFF
    -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
EMCMAKE_PREFIX=""

case "$TARGET_PLATFORM" in

linux-x86_64)
    : # native gcc
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
    )
    ;;

linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=riscv64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
    )
    ;;

macos-x86_64)
    CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64")
    ;;

macos-arm64)
    CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")
    ;;

ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64)
    # nix-on-macOS xcrun trap.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"
    : "${IOS_MIN:=15.0}"
    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)   _IOS_SDK="iphoneos";         _IOS_ARCH="arm64"
                     _CMAKE_SYS="iOS";  _CMAKE_DEPL="$IOS_MIN" ;;
        ios-x86_64)  _IOS_SDK="iphonesimulator";  _IOS_ARCH="x86_64"
                     _CMAKE_SYS="iOS";  _CMAKE_DEPL="$IOS_MIN" ;;
        tvos-x86_64) _IOS_SDK="appletvsimulator"; _IOS_ARCH="x86_64"
                     _CMAKE_SYS="tvOS"; _CMAKE_DEPL="$TVOS_MIN" ;;
        tvos-arm64)  _IOS_SDK="appletvos"       ; _IOS_ARCH="arm64"
                     _CMAKE_SYS="tvOS"; _CMAKE_DEPL="$TVOS_MIN" ;;
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
    # Native MSVC — caller must have vcvarsall'd the shell. cmake auto-
    # detects cl.exe.
    : # cmake's default Ninja+cl pickup is fine
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Configure + build + install
#-----------------------------------------------------------------------------
echo "==> configuring libssh2 ${VERSION} for $TARGET_PLATFORM (openssl ${OSSL_VERSION})"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"

echo "==> building (-j${NCPU})"
cmake --build "$BUILD_DIR" -j"$NCPU"

echo "==> installing"
cmake --install "$BUILD_DIR"

#-----------------------------------------------------------------------------
# Stage + verify. libssh2 cmake installs:
#   - lib{,64}/libssh2.a
#   - include/libssh2.h, libssh2_publickey.h, libssh2_sftp.h
#   - lib/pkgconfig/libssh2.pc + lib/cmake/libssh2/...  (we drop these)
#-----------------------------------------------------------------------------
mkdir -p "$STAGE/lib" "$STAGE/include"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        find "$INSTALL_DIR/$_D" -maxdepth 1 -name 'libssh2*' \( -name '*.a' -o -name '*.lib' \) \
            -exec cp -a {} "$STAGE/lib/" \;
    fi
done
cp -a "$INSTALL_DIR/include/." "$STAGE/include/"

if [ ! -f "$STAGE/lib/libssh2.a" ] && [ ! -f "$STAGE/lib/libssh2.lib" ]; then
    echo "missing libssh2.a / libssh2.lib in $STAGE/lib/ — install layout changed?" >&2
    find "$INSTALL_DIR" -maxdepth 4 -print >&2 || true
    exit 1
fi
[ -f "$STAGE/include/libssh2.h" ] || { echo "missing libssh2.h" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "libssh2 $VERSION ($TARGET_PLATFORM, openssl $OSSL_VERSION) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents ($ENTRIES files):"
tar -tzf "$TARBALL"
