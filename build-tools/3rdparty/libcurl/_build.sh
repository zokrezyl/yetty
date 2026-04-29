#!/bin/bash
# Builds libcurl (curl/curl) for $TARGET_PLATFORM using the upstream
# CMake project, statically linked against OpenSSL 4 (from the
# `lib-openssl-new-<ver>` GitHub release — fetched here at build time).
#
# Replaces the per-yetty-configure `find_package(CURL)` system-libcurl
# fallback in build-tools/cmake/libs/libcurl.cmake. Removes the
# Windows/cross "no system libcurl available" gap.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 | tvos-x86_64 |
#                     webasm | windows-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Version is read from this directory's `version` file. The OpenSSL
# version pin used as TLS backend comes from
# build-tools/3rdparty/openssl-new/version (single source of truth so
# bumping openssl-new + libcurl together stays consistent).
#
# Optional env:
#   WORK_DIR          default /tmp/yetty-3rdparty-libcurl-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#   ANDROID_API       default 26
#   IOS_MIN           default 15.0
#   CROSS_PREFIX      default aarch64-unknown-linux-gnu- (linux-aarch64)
#   GH_RELEASE_BASE   default https://github.com/zokrezyl/yetty/releases/download
#                     (override only for testing against a private fork)
#
# Output tarball layout (consumed by build-tools/cmake/libs/libcurl.cmake):
#   lib/libcurl.a (or curl.lib on windows native MSVC)
#   include/curl/*.h

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

OPENSSL_VERSION_FILE="$REPO_ROOT/build-tools/3rdparty/openssl-new/version"
[ -f "$OPENSSL_VERSION_FILE" ] || { echo "missing $OPENSSL_VERSION_FILE" >&2; exit 1; }
OPENSSL_NEW_VERSION="$(tr -d '[:space:]' < "$OPENSSL_VERSION_FILE")"
[ -n "$OPENSSL_NEW_VERSION" ] || { echo "$OPENSSL_VERSION_FILE is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libcurl-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
GH_RELEASE_BASE="${GH_RELEASE_BASE:-https://github.com/zokrezyl/yetty/releases/download}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Upstream curl/curl: tag is curl-<ver-with-underscores> on github.
# Tarball top-level dir: curl-<dotted-ver>.
CURL_VER_TAG="$(echo "$VERSION" | tr '.' '_')"
CURL_URL="https://github.com/curl/curl/archive/refs/tags/curl-${CURL_VER_TAG}.tar.gz"
CURL_TARBALL="$CACHE_DIR/curl-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/curl-curl-${CURL_VER_TAG}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libcurl-${TARGET_PLATFORM}-${VERSION}.tar.gz"

# OpenSSL prebuilt fetch — from the lib-openssl-new-<ver> release.
OSSL_FILENAME="openssl-new-${TARGET_PLATFORM}-${OPENSSL_NEW_VERSION}.tar.gz"
OSSL_TARBALL="$CACHE_DIR/$OSSL_FILENAME"
OSSL_URL="${GH_RELEASE_BASE}/lib-openssl-new-${OPENSSL_NEW_VERSION}/${OSSL_FILENAME}"
OSSL_DIR="$WORK_DIR/openssl-new-${OPENSSL_NEW_VERSION}"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch OpenSSL prebuilt for this target
#-----------------------------------------------------------------------------
if [ ! -f "$OSSL_TARBALL" ]; then
    _part="$OSSL_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$OSSL_TARBALL" ]; then
            echo "==> downloading openssl-new ${OPENSSL_NEW_VERSION} for ${TARGET_PLATFORM}"
            echo "    $OSSL_URL"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$OSSL_URL"
            mv "$_part" "$OSSL_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.openssl-new-download.lock"
    rm -f "$_part"
fi
if [ ! -d "$OSSL_DIR" ]; then
    echo "==> extracting openssl-new -> $OSSL_DIR"
    mkdir -p "$OSSL_DIR"
    tar -C "$OSSL_DIR" -xzf "$OSSL_TARBALL"
fi
# Windows MSVC build of openssl-new ships libssl.lib + libcrypto.lib;
# everywhere else the convention is libssl.a + libcrypto.a.
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    _OSSL_SSL="$OSSL_DIR/lib/libssl.lib"
    _OSSL_CRYPTO="$OSSL_DIR/lib/libcrypto.lib"
else
    _OSSL_SSL="$OSSL_DIR/lib/libssl.a"
    _OSSL_CRYPTO="$OSSL_DIR/lib/libcrypto.a"
fi
[ -f "$_OSSL_SSL"    ] || { echo "openssl-new: missing $(basename "$_OSSL_SSL")"    >&2; exit 1; }
[ -f "$_OSSL_CRYPTO" ] || { echo "openssl-new: missing $(basename "$_OSSL_CRYPTO")" >&2; exit 1; }
[ -f "$OSSL_DIR/include/openssl/ssl.h" ] || { echo "openssl-new: missing ssl.h" >&2; exit 1; }

#-----------------------------------------------------------------------------
# Fetch curl
#-----------------------------------------------------------------------------
if [ ! -f "$CURL_TARBALL" ]; then
    _part="$CURL_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$CURL_TARBALL" ]; then
            echo "==> downloading curl ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$CURL_URL"
            mv "$_part" "$CURL_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.libcurl-download.lock"
    rm -f "$_part"
else
    echo "==> using cached curl source: $CURL_TARBALL"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$CURL_TARBALL"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

#-----------------------------------------------------------------------------
# CMake args. Static-only, OpenSSL backend, minimal protocol surface
# (yetty's ycat needs HTTP/HTTPS only). Disable system-detected optional
# deps that would vary by host (zstd, brotli, libpsl, libssh2, libidn2,
# nghttp2) — keep tarballs uniform across platforms.
#-----------------------------------------------------------------------------
CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_STATIC_LIBS=ON
    -DBUILD_CURL_EXE=OFF
    -DBUILD_TESTING=OFF
    -DBUILD_LIBCURL_DOCS=OFF
    -DBUILD_MISC_DOCS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON

    -DCURL_USE_OPENSSL=ON
    -DOPENSSL_ROOT_DIR="$OSSL_DIR"
    -DOPENSSL_INCLUDE_DIR="$OSSL_DIR/include"
    -DOPENSSL_SSL_LIBRARY="$_OSSL_SSL"
    -DOPENSSL_CRYPTO_LIBRARY="$_OSSL_CRYPTO"

    # Flag-name conventions are mixed in curl 8.x: CURL_USE_* for some,
    # USE_* for others (libidn2, nghttp2, librtmp). Audit confirmed
    # against curl 8.19.0's CMakeLists.txt option() declarations.
    -DCURL_USE_LIBSSH2=OFF
    -DCURL_USE_LIBPSL=OFF
    -DUSE_LIBIDN2=OFF
    -DCURL_USE_GNUTLS=OFF
    -DCURL_USE_MBEDTLS=OFF
    -DCURL_USE_WOLFSSL=OFF
    -DCURL_USE_BEARSSL=OFF
    -DCURL_DISABLE_LDAP=ON
    -DCURL_DISABLE_LDAPS=ON
    -DCURL_BROTLI=OFF
    -DCURL_ZSTD=OFF
    -DCURL_ZLIB=OFF
    -DUSE_NGHTTP2=OFF
    -DUSE_NGTCP2=OFF
    -DUSE_QUICHE=OFF
    -DUSE_LIBRTMP=OFF
    -DENABLE_UNIX_SOCKETS=OFF
    # Belt-and-suspenders: tell CMake's find_package() to refuse the
    # auto-detected nix-store deps even if upstream curl ever flips
    # one of the above to ON by default.
    -DCMAKE_DISABLE_FIND_PACKAGE_Libidn2=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_NGHTTP2=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_LibPSL=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_LibSSH2=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_BROTLI=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_ZSTD=ON
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
    # Curl on emscripten: no native sockets, builds with --enable-websocket
    # backend by default (handled by emscripten). Disable threaded resolver
    # — emscripten threading is fragile.
    CMAKE_ARGS+=(
        "-DENABLE_THREADED_RESOLVER=OFF"
    )
    ;;

windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cmake picks
    # up cl.exe via auto-detection. SCHANNEL off so curl uses our
    # prebuilt OpenSSL only.
    CMAKE_ARGS+=("-DCURL_USE_SCHANNEL=OFF")
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Configure + build + install
#-----------------------------------------------------------------------------
echo "==> configuring libcurl ${VERSION} (TLS: openssl-new ${OPENSSL_NEW_VERSION}) for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"

echo "==> building (-j${NCPU})"
cmake --build "$BUILD_DIR" -j"$NCPU"

echo "==> installing"
cmake --install "$BUILD_DIR"

#-----------------------------------------------------------------------------
# Stage + verify
#-----------------------------------------------------------------------------
mkdir -p "$STAGE/lib"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        cp -a "$INSTALL_DIR/$_D/." "$STAGE/lib/"
    fi
done
cp -a "$INSTALL_DIR/include" "$STAGE/"

# Accept libcurl.a (unix/MSYS2 CLANG64) or libcurl.lib (windows native MSVC)
_LIB_FOUND=""
for _CAND in "$STAGE/lib/libcurl.a" "$STAGE/lib/libcurl.lib" "$STAGE/lib/curl.lib"; do
    if [ -f "$_CAND" ]; then _LIB_FOUND="$_CAND"; break; fi
done
if [ -z "$_LIB_FOUND" ]; then
    echo "missing libcurl static lib in stage" >&2
    find "$STAGE" -maxdepth 4 -print >&2 || true
    exit 1
fi
if [ ! -f "$STAGE/include/curl/curl.h" ]; then
    echo "missing headers: $STAGE/include/curl/curl.h" >&2
    exit 1
fi

# Drop the lib/cmake/CURL/ tree — yetty's consumer cmake doesn't use the
# CURLConfig.cmake from there; saves ~10 KB.
rm -rf "$STAGE/lib/cmake" "$STAGE/lib/pkgconfig"

# Also drop libcurl.so* if the system happened to produce them (we
# requested static-only but some distros' CMake snapshots include shared).
find "$STAGE/lib" -maxdepth 1 -name 'libcurl.so*' -delete 2>/dev/null || true

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "libcurl $VERSION + openssl-new ${OPENSSL_NEW_VERSION} ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 25 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,25p'
