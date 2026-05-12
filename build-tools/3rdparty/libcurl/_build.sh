#!/bin/bash
# Builds libcurl (curl/curl) for $TARGET_PLATFORM using the upstream
# CMake project, statically linked against three prebuilt yetty 3rdparty
# tarballs fetched from GitHub releases at build time:
#   - openssl-new   (TLS backend; lib-openssl-new-<ver>)
#   - zlib          (gzip Content-Encoding;  lib-zlib-<ver>)
#   - brotli        (br   Content-Encoding;  lib-brotli-<ver>)
# All three are *also* consumed independently by yetty's main build
# (build-tools/cmake/libs/{openssl-new,zlib,brotli}.cmake) — same
# tarballs, single source of truth via each subdir's `version` file.
#
# The output tarball ships ONLY `lib/libcurl.a` + headers; the static
# archive carries unresolved zlib + brotli + openssl symbols, which the
# yetty consumer side resolves transitively via libcurl.cmake's link
# interface (which includes the matching libs/{zlib,brotli}.cmake).
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 | tvos-x86_64 |
#                     webasm | windows-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# libcurl version is read from this directory's `version` file. The
# OpenSSL/zlib/brotli pins come from each library's own
# `build-tools/3rdparty/<lib>/version` file.
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

# zlib + brotli versions come from their respective 3rdparty version
# files — single source of truth so bumping them propagates everywhere.
# These are the same prebuilt tarballs yetty's main build consumes via
# build-tools/cmake/libs/{zlib,brotli}.cmake; we link the same archives
# into libcurl so symbol resolution at the final yetty link stage finds
# only one copy.
ZLIB_VERSION_FILE="$REPO_ROOT/build-tools/3rdparty/zlib/version"
[ -f "$ZLIB_VERSION_FILE" ] || { echo "missing $ZLIB_VERSION_FILE" >&2; exit 1; }
ZLIB_VERSION="$(tr -d '[:space:]' < "$ZLIB_VERSION_FILE")"
[ -n "$ZLIB_VERSION" ] || { echo "$ZLIB_VERSION_FILE is empty" >&2; exit 1; }

BROTLI_VERSION_FILE="$REPO_ROOT/build-tools/3rdparty/brotli/version"
[ -f "$BROTLI_VERSION_FILE" ] || { echo "missing $BROTLI_VERSION_FILE" >&2; exit 1; }
BROTLI_VERSION="$(tr -d '[:space:]' < "$BROTLI_VERSION_FILE")"
[ -n "$BROTLI_VERSION" ] || { echo "$BROTLI_VERSION_FILE is empty" >&2; exit 1; }

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

ZLIB_FILENAME="zlib-${TARGET_PLATFORM}-${ZLIB_VERSION}.tar.gz"
ZLIB_TARBALL="$CACHE_DIR/$ZLIB_FILENAME"
ZLIB_URL="${GH_RELEASE_BASE}/lib-zlib-${ZLIB_VERSION}/${ZLIB_FILENAME}"
ZLIB_DIR="$WORK_DIR/zlib-${ZLIB_VERSION}"

BROTLI_FILENAME="brotli-${TARGET_PLATFORM}-${BROTLI_VERSION}.tar.gz"
BROTLI_TARBALL="$CACHE_DIR/$BROTLI_FILENAME"
BROTLI_URL="${GH_RELEASE_BASE}/lib-brotli-${BROTLI_VERSION}/${BROTLI_FILENAME}"
BROTLI_DIR="$WORK_DIR/brotli-${BROTLI_VERSION}"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

# HTTP/2 + HTTP/3 backends — built INLINE from upstream sources (no
# yetty 3rdparty tarballs exist for these yet). Static-linked into the
# libcurl tarball, just like openssl/zlib/brotli. Gated on platforms
# where QUIC genuinely works:
#   - linux-{x86_64,aarch64,riscv64}: native UDP sockets, autotools build
#   - macos-{x86_64,arm64}            : same
# Other platforms (android/ios/tvos/webasm/windows) skip QUIC for now —
# their toolchains/socket layers need per-platform work. libcurl on those
# stays at HTTP/1.1 + HTTP/2 (the latter when nghttp2 builds there
# eventually). HTTP/3 needs OpenSSL ≥ 3.5 for its QUIC API — openssl-new
# 4.0+ has it via the ossl crypto backend in ngtcp2.
NGHTTP2_VERSION="${NGHTTP2_VERSION:-1.67.1}"
NGHTTP3_VERSION="${NGHTTP3_VERSION:-1.11.0}"
NGTCP2_VERSION="${NGTCP2_VERSION:-1.15.0}"

BUILD_QUIC=0
case "$TARGET_PLATFORM" in
    linux-x86_64|linux-aarch64|linux-riscv64|macos-x86_64|macos-arm64)
        BUILD_QUIC=1
        ;;
esac

# Cross-compilation triplet — set early so both the inline autoconf deps
# (nghttp2/nghttp3/ngtcp2) and the CMake curl build use the same prefix.
# The :=default guards in the cmake section below are kept as user override
# points and won't reassign once CROSS_PREFIX is already set here.
AUTOCONF_HOST=""
case "$TARGET_PLATFORM" in
    linux-aarch64)
        : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
        AUTOCONF_HOST="${CROSS_PREFIX%-}"
        ;;
    linux-riscv64)
        : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
        AUTOCONF_HOST="${CROSS_PREFIX%-}"
        ;;
esac

HTTP_PREFIX="$WORK_DIR/http-deps-${TARGET_PLATFORM}"

# Inline-source-fetch helper — github.com/<repo> release tarball into
# $WORK_DIR. Same retry policy as fetch_3rdparty; keeps the source in
# $CACHE_DIR for re-runs.
fetch_inline_src() {
    local _name="$1" _url="$2" _src_dir="$3"
    local _cache="$CACHE_DIR/${_name}.tar.gz"
    if [ ! -f "$_cache" ]; then
        local _part="$_cache.part.$$"
        (
            if command -v flock >/dev/null 2>&1; then flock -x 9; fi
            if [ ! -f "$_cache" ]; then
                echo "==> downloading $_name"
                curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                    -o "$_part" "$_url"
                mv "$_part" "$_cache"
            fi
        ) 9>"$CACHE_DIR/.${_name}-download.lock"
        rm -f "$_part"
    fi
    if [ ! -d "$_src_dir" ]; then
        echo "==> extracting $_name -> $_src_dir"
        local _tmp="$WORK_DIR/.unpack-$$"
        mkdir -p "$_tmp"
        tar -C "$_tmp" -xzf "$_cache"
        # tarball top-level dir is the only entry — move it to expected name
        local _top
        _top="$(ls -1 "$_tmp" | head -n1)"
        mv "$_tmp/$_top" "$_src_dir"
        rmdir "$_tmp"
    fi
}

if [ "$BUILD_QUIC" = "1" ]; then
    rm -rf "$HTTP_PREFIX"
    mkdir -p "$HTTP_PREFIX"

    # nghttp2 — HTTP/2 framing
    NGHTTP2_SRC="$WORK_DIR/nghttp2-${NGHTTP2_VERSION}"
    fetch_inline_src "nghttp2-${NGHTTP2_VERSION}" \
        "https://github.com/nghttp2/nghttp2/releases/download/v${NGHTTP2_VERSION}/nghttp2-${NGHTTP2_VERSION}.tar.gz" \
        "$NGHTTP2_SRC"
    echo "==> building nghttp2 ${NGHTTP2_VERSION}"
    (cd "$NGHTTP2_SRC" && \
        ./configure --prefix="$HTTP_PREFIX" \
            ${AUTOCONF_HOST:+--host="$AUTOCONF_HOST" CC="${CROSS_PREFIX}gcc" CXX="${CROSS_PREFIX}g++"} \
            --enable-static --disable-shared \
            --enable-lib-only \
            --without-libxml2 --without-jansson \
            CFLAGS="-fPIC" CXXFLAGS="-fPIC" >/dev/null && \
        make -j"$NCPU" >/dev/null && \
        make install >/dev/null)

    # nghttp3 — HTTP/3 framing over QUIC. Standalone, no openssl dep.
    NGHTTP3_SRC="$WORK_DIR/nghttp3-${NGHTTP3_VERSION}"
    fetch_inline_src "nghttp3-${NGHTTP3_VERSION}" \
        "https://github.com/ngtcp2/nghttp3/releases/download/v${NGHTTP3_VERSION}/nghttp3-${NGHTTP3_VERSION}.tar.gz" \
        "$NGHTTP3_SRC"
    echo "==> building nghttp3 ${NGHTTP3_VERSION}"
    (cd "$NGHTTP3_SRC" && \
        ./configure --prefix="$HTTP_PREFIX" \
            ${AUTOCONF_HOST:+--host="$AUTOCONF_HOST" CC="${CROSS_PREFIX}gcc" CXX="${CROSS_PREFIX}g++"} \
            --enable-static --disable-shared --enable-lib-only \
            CFLAGS="-fPIC" CXXFLAGS="-fPIC" >/dev/null && \
        make -j"$NCPU" >/dev/null && \
        make install >/dev/null)

    # ngtcp2 — QUIC transport. Uses our openssl-new for the crypto
    # backend (requires OpenSSL ≥ 3.5 QUIC API; openssl-new is 4.0+).
    NGTCP2_SRC="$WORK_DIR/ngtcp2-${NGTCP2_VERSION}"
    fetch_inline_src "ngtcp2-${NGTCP2_VERSION}" \
        "https://github.com/ngtcp2/ngtcp2/releases/download/v${NGTCP2_VERSION}/ngtcp2-${NGTCP2_VERSION}.tar.gz" \
        "$NGTCP2_SRC"
    echo "==> building ngtcp2 ${NGTCP2_VERSION} (openssl QUIC backend)"
    (cd "$NGTCP2_SRC" && \
        PKG_CONFIG_PATH="$OSSL_DIR/lib/pkgconfig:$OSSL_DIR/lib64/pkgconfig" \
        ./configure --prefix="$HTTP_PREFIX" \
            ${AUTOCONF_HOST:+--host="$AUTOCONF_HOST" CC="${CROSS_PREFIX}gcc" CXX="${CROSS_PREFIX}g++"} \
            --enable-static --disable-shared --enable-lib-only \
            --with-openssl="$OSSL_DIR" \
            CFLAGS="-fPIC -I$OSSL_DIR/include" \
            CXXFLAGS="-fPIC -I$OSSL_DIR/include" \
            LDFLAGS="-L$OSSL_DIR/lib -L$OSSL_DIR/lib64" \
            OPENSSL_CFLAGS="-I$OSSL_DIR/include" \
            OPENSSL_LIBS="-L$OSSL_DIR/lib -L$OSSL_DIR/lib64 -lssl -lcrypto" >/dev/null && \
        make -j"$NCPU" >/dev/null && \
        make install >/dev/null)

    # Verify the .a archives we'll be feeding to libcurl + the final
    # yetty link line actually got built.
    for _F in "$HTTP_PREFIX/lib/libnghttp2.a" \
              "$HTTP_PREFIX/lib/libnghttp3.a" \
              "$HTTP_PREFIX/lib/libngtcp2.a" \
              "$HTTP_PREFIX/lib/libngtcp2_crypto_ossl.a"; do
        [ -f "$_F" ] || { echo "missing: $_F" >&2; exit 1; }
    done
fi


# fetch_3rdparty <name> <tarball-cache-path> <url> <extract-dir>
fetch_3rdparty() {
    local _name="$1" _cache="$2" _url="$3" _dir="$4"
    if [ ! -f "$_cache" ]; then
        local _part="$_cache.part.$$"
        (
            if command -v flock >/dev/null 2>&1; then flock -x 9; fi
            if [ ! -f "$_cache" ]; then
                echo "==> downloading $_name for ${TARGET_PLATFORM}"
                echo "    $_url"
                curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                    -o "$_part" "$_url"
                mv "$_part" "$_cache"
            fi
        ) 9>"$CACHE_DIR/.${_name}-download.lock"
        rm -f "$_part"
    fi
    if [ ! -d "$_dir" ]; then
        echo "==> extracting $_name -> $_dir"
        mkdir -p "$_dir"
        tar -C "$_dir" -xzf "$_cache"
    fi
}

#-----------------------------------------------------------------------------
# Fetch prebuilt deps (OpenSSL TLS, zlib for gzip, brotli for br)
#-----------------------------------------------------------------------------
fetch_3rdparty openssl-new "$OSSL_TARBALL"   "$OSSL_URL"   "$OSSL_DIR"
fetch_3rdparty zlib        "$ZLIB_TARBALL"   "$ZLIB_URL"   "$ZLIB_DIR"
fetch_3rdparty brotli      "$BROTLI_TARBALL" "$BROTLI_URL" "$BROTLI_DIR"

# Windows MSVC build of openssl-new ships libssl.lib + libcrypto.lib;
# everywhere else the convention is libssl.a + libcrypto.a.
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    _OSSL_SSL="$OSSL_DIR/lib/libssl.lib"
    _OSSL_CRYPTO="$OSSL_DIR/lib/libcrypto.lib"
    _ZLIB_LIB="$ZLIB_DIR/lib/zlibstatic.lib"
    _BROTLI_DEC="$BROTLI_DIR/lib/brotlidec.lib"
    _BROTLI_COMMON="$BROTLI_DIR/lib/brotlicommon.lib"
else
    _OSSL_SSL="$OSSL_DIR/lib/libssl.a"
    _OSSL_CRYPTO="$OSSL_DIR/lib/libcrypto.a"
    _ZLIB_LIB="$ZLIB_DIR/lib/libz.a"
    _BROTLI_DEC="$BROTLI_DIR/lib/libbrotlidec.a"
    _BROTLI_COMMON="$BROTLI_DIR/lib/libbrotlicommon.a"
fi
[ -f "$_OSSL_SSL"      ] || { echo "openssl-new: missing $(basename "$_OSSL_SSL")"     >&2; exit 1; }
[ -f "$_OSSL_CRYPTO"   ] || { echo "openssl-new: missing $(basename "$_OSSL_CRYPTO")"  >&2; exit 1; }
[ -f "$OSSL_DIR/include/openssl/ssl.h"   ] || { echo "openssl-new: missing ssl.h"     >&2; exit 1; }
[ -f "$_ZLIB_LIB"      ] || { echo "zlib: missing $(basename "$_ZLIB_LIB")"            >&2; exit 1; }
[ -f "$ZLIB_DIR/include/zlib.h"          ] || { echo "zlib: missing zlib.h"            >&2; exit 1; }
[ -f "$_BROTLI_DEC"    ] || { echo "brotli: missing $(basename "$_BROTLI_DEC")"        >&2; exit 1; }
[ -f "$_BROTLI_COMMON" ] || { echo "brotli: missing $(basename "$_BROTLI_COMMON")"     >&2; exit 1; }
[ -f "$BROTLI_DIR/include/brotli/decode.h" ] || { echo "brotli: missing decode.h"      >&2; exit 1; }

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

    # zlib (gzip) — curl's FindZLIB reads ZLIB_INCLUDE_DIR + ZLIB_LIBRARY.
    -DCURL_ZLIB=ON
    -DZLIB_INCLUDE_DIR="$ZLIB_DIR/include"
    -DZLIB_LIBRARY="$_ZLIB_LIB"

    # brotli (br) — curl 8.x's CMake/FindBrotli.cmake reads
    # BROTLI_INCLUDE_DIR + BROTLIDEC_LIBRARY + BROTLICOMMON_LIBRARY
    # (note: NO underscore between BROTLI and DEC/COMMON — easy to
    # miswrite). Decoder + common are enough for incoming responses
    # (curl never *encodes* request bodies as br).
    -DCURL_BROTLI=ON
    -DBROTLI_INCLUDE_DIR="$BROTLI_DIR/include"
    -DBROTLIDEC_LIBRARY="$_BROTLI_DEC"
    -DBROTLICOMMON_LIBRARY="$_BROTLI_COMMON"

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
    -DCURL_ZSTD=OFF
    -DUSE_QUICHE=OFF
    -DUSE_LIBRTMP=OFF
    -DENABLE_UNIX_SOCKETS=OFF
    # Belt-and-suspenders: tell CMake's find_package() to refuse the
    # auto-detected nix-store deps even if upstream curl ever flips
    # one of the above to ON by default.
    -DCMAKE_DISABLE_FIND_PACKAGE_Libidn2=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_LibPSL=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_LibSSH2=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_ZSTD=ON
)
if [ "$BUILD_QUIC" = "1" ]; then
    # Hand curl our inline-built nghttp2 + nghttp3 + ngtcp2 (+ ossl
    # crypto backend). Variable names track curl 8.x's
    # FindNGHTTP2/FindNGHTTP3/FindNGTCP2 conventions.
    CMAKE_ARGS+=(
        -DUSE_NGHTTP2=ON
        -DNGHTTP2_INCLUDE_DIR="$HTTP_PREFIX/include"
        -DNGHTTP2_LIBRARY="$HTTP_PREFIX/lib/libnghttp2.a"

        -DUSE_NGHTTP3=ON
        -DNGHTTP3_INCLUDE_DIR="$HTTP_PREFIX/include"
        -DNGHTTP3_LIBRARY="$HTTP_PREFIX/lib/libnghttp3.a"

        -DUSE_NGTCP2=ON
        -DNGTCP2_INCLUDE_DIR="$HTTP_PREFIX/include"
        -DNGTCP2_LIBRARY="$HTTP_PREFIX/lib/libngtcp2.a"
        -DNGTCP2_CRYPTO_OSSL_LIBRARY="$HTTP_PREFIX/lib/libngtcp2_crypto_ossl.a"
        # Some curl versions look for the crypto backend lib under a
        # different cache-var; set both so either matches.
        -DNGTCP2_CRYPTO_LIBRARY="$HTTP_PREFIX/lib/libngtcp2_crypto_ossl.a"
    )
else
    CMAKE_ARGS+=(
        -DUSE_NGHTTP2=OFF
        -DUSE_NGTCP2=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_NGHTTP2=ON
    )
fi
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

# Stage the QUIC sidecar archives next to libcurl.a — libcurl.a has
# unresolved nghttp2/nghttp3/ngtcp2/ngtcp2_crypto_ossl symbols and the
# consumer cmake (libs/libcurl.cmake) needs to link these in. Without
# this the final yetty binary would fail to link with undefined refs
# to nghttp2_session_*, ngtcp2_conn_*, nghttp3_conn_*.
if [ "$BUILD_QUIC" = "1" ]; then
    for _A in libnghttp2.a libnghttp3.a libngtcp2.a libngtcp2_crypto_ossl.a; do
        cp -a "$HTTP_PREFIX/lib/$_A" "$STAGE/lib/"
    done
    # Headers aren't strictly needed by consumers (they don't include
    # them directly), but shipping the include/ngtcp2 et al. is cheap
    # and lets debug builds / introspection tools resolve symbols.
    for _H in nghttp2 nghttp3 ngtcp2; do
        if [ -d "$HTTP_PREFIX/include/$_H" ]; then
            cp -a "$HTTP_PREFIX/include/$_H" "$STAGE/include/"
        fi
    done
fi

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
