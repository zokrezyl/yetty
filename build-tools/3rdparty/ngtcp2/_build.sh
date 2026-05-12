#!/bin/bash
# Builds ngtcp2 (ngtcp2/ngtcp2) for $TARGET_PLATFORM. QUIC transport
# library — consumed by libcurl for HTTP/3. Produces both libngtcp2.a
# (core) and libngtcp2_crypto_ossl.a (OpenSSL QUIC TLS bridge).
#
# Native build (no cross-compile, no Nix) — see nghttp2/_build.sh header.
#
# Depends on the openssl-new prebuilt tarball (fetched at build time from
# GitHub releases — same pattern as libcurl/_build.sh's openssl fetch).
# The version pin is read from build-tools/3rdparty/openssl-new/version,
# so bumping that file is enough to migrate both libs together.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | linux-riscv64 |
#                     macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Optional env:
#   GH_RELEASE_BASE   default https://github.com/zokrezyl/yetty/releases/download
#                     (override only for testing against a private fork)
#
# Output tarball layout (consumed by libcurl/_build.sh):
#   lib/libngtcp2.a
#   lib/libngtcp2_crypto_ossl.a
#   lib/pkgconfig/*.pc
#   include/ngtcp2/*.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

OPENSSL_VERSION_FILE="$REPO_ROOT/build-tools/3rdparty/openssl-new/version"
[ -f "$OPENSSL_VERSION_FILE" ] || { echo "missing $OPENSSL_VERSION_FILE" >&2; exit 1; }
OPENSSL_NEW_VERSION="$(tr -d '[:space:]' < "$OPENSSL_VERSION_FILE")"
[ -n "$OPENSSL_NEW_VERSION" ] || { echo "$OPENSSL_VERSION_FILE is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-ngtcp2-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
GH_RELEASE_BASE="${GH_RELEASE_BASE:-https://github.com/zokrezyl/yetty/releases/download}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL="https://github.com/ngtcp2/ngtcp2/releases/download/v${VERSION}/ngtcp2-${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/ngtcp2-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/ngtcp2-${VERSION}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/ngtcp2-${TARGET_PLATFORM}-${VERSION}.tar.gz"

OSSL_FILENAME="openssl-new-${TARGET_PLATFORM}-${OPENSSL_NEW_VERSION}.tar.gz"
OSSL_TARBALL="$CACHE_DIR/$OSSL_FILENAME"
OSSL_URL="${GH_RELEASE_BASE}/lib-openssl-new-${OPENSSL_NEW_VERSION}/${OSSL_FILENAME}"
OSSL_DIR="$WORK_DIR/openssl-new-${OPENSSL_NEW_VERSION}"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

# Fetch openssl-new prebuilt — same release tag the libcurl build pulls.
if [ ! -f "$OSSL_TARBALL" ]; then
    echo "==> downloading openssl-new ${OPENSSL_NEW_VERSION} for ${TARGET_PLATFORM}"
    echo "    $OSSL_URL"
    _part="$OSSL_TARBALL.part.$$"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$OSSL_URL"
    mv "$_part" "$OSSL_TARBALL"
fi
if [ ! -d "$OSSL_DIR" ]; then
    echo "==> extracting openssl-new -> $OSSL_DIR"
    mkdir -p "$OSSL_DIR"
    tar -C "$OSSL_DIR" -xzf "$OSSL_TARBALL"
fi
[ -f "$OSSL_DIR/lib/libssl.a"    ] || { echo "openssl-new: missing libssl.a in $OSSL_DIR/lib" >&2; ls -la "$OSSL_DIR/lib" >&2 || true; exit 1; }
[ -f "$OSSL_DIR/lib/libcrypto.a" ] || { echo "openssl-new: missing libcrypto.a in $OSSL_DIR/lib" >&2; exit 1; }
[ -f "$OSSL_DIR/include/openssl/ssl.h" ] || { echo "openssl-new: missing ssl.h" >&2; exit 1; }

# Rewrite absolute prefix= in OpenSSL's .pc files. OpenSSL's Configure
# bakes the build-machine's INSTALL_DIR path into prefix=, which is dead
# on every other machine. ngtcp2's configure uses PKG_CHECK_MODULES to
# locate openssl, then AC_LINK_IFELSE compiles a test program against
# the returned -I/-L paths. With stale paths the includes don't resolve,
# SSL_set_quic_tls_cbs registers as "no", and the whole configure aborts
# with "openssl was requested but not found".
for _pc in "$OSSL_DIR"/lib/pkgconfig/*.pc "$OSSL_DIR"/lib64/pkgconfig/*.pc; do
    [ -f "$_pc" ] || continue
    sed -i.bak "s|^prefix=.*|prefix=$OSSL_DIR|" "$_pc"
    rm -f "$_pc.bak"
done

# Fetch ngtcp2.
if [ ! -f "$TARBALL_CACHE" ]; then
    echo "==> downloading ngtcp2 ${VERSION}"
    _part="$TARBALL_CACHE.part.$$"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
    mv "$_part" "$TARBALL_CACHE"
fi
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"
fi
rm -rf "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

# ngtcp2's configure.ac uses PKG_CHECK_MODULES([OPENSSL], [openssl>=1.1.1])
# to locate OpenSSL, then AC_LINK_IFELSE-tests for SSL_set_quic_tls_cbs
# (the OpenSSL 3.5+ QUIC TLS API). On have_ossl=yes, the build adds the
# crypto/ossl subdir and produces libngtcp2_crypto_ossl.a. We point
# PKG_CONFIG_PATH at the openssl-new tarball's .pc files and let configure
# discover everything. NOT passing --enable-lib-only because in 1.15.0
# the "examples" subdir requires app-only deps (libev/picotls) that we
# don't ship — but the apps + examples are gated by ENABLE_EXAMPLE_*
# AM_CONDITIONALs that go false when those deps aren't found, so the
# build skips them automatically.

PKGCFG_PATH=""
for _d in "$OSSL_DIR/lib/pkgconfig" "$OSSL_DIR/lib64/pkgconfig"; do
    if [ -d "$_d" ]; then
        PKGCFG_PATH="${PKGCFG_PATH:+$PKGCFG_PATH:}$_d"
    fi
done
[ -n "$PKGCFG_PATH" ] || { echo "openssl-new tarball has no pkgconfig/ dir; cannot locate openssl.pc" >&2; exit 1; }

echo "==> configuring ngtcp2 ${VERSION} for $TARGET_PLATFORM (openssl QUIC backend)"
echo "    PKG_CONFIG_PATH=$PKGCFG_PATH"
(cd "$SRC_DIR" && \
    PKG_CONFIG_PATH="$PKGCFG_PATH" \
    ./configure --prefix="$INSTALL_DIR" \
        --enable-static --disable-shared \
        --with-openssl \
        CFLAGS="-fPIC" \
        CXXFLAGS="-fPIC")

echo "==> building ngtcp2"
make -C "$SRC_DIR" -j"$NCPU"
make -C "$SRC_DIR" install

mkdir -p "$STAGE/lib" "$STAGE/include"
cp -a "$INSTALL_DIR/lib/libngtcp2.a" "$STAGE/lib/"
cp -a "$INSTALL_DIR/lib/libngtcp2_crypto_ossl.a" "$STAGE/lib/"
cp -a "$INSTALL_DIR/include/ngtcp2" "$STAGE/include/"
if [ -d "$INSTALL_DIR/lib/pkgconfig" ]; then
    mkdir -p "$STAGE/lib/pkgconfig"
    cp -a "$INSTALL_DIR/lib/pkgconfig/." "$STAGE/lib/pkgconfig/"
fi

for _F in "$STAGE/lib/libngtcp2.a" \
          "$STAGE/lib/libngtcp2_crypto_ossl.a" \
          "$STAGE/include/ngtcp2/ngtcp2.h"; do
    [ -f "$_F" ] || { echo "missing: $_F" >&2; exit 1; }
done

tar -C "$STAGE" -czf "$TARBALL" .
echo "ngtcp2 $VERSION ($TARGET_PLATFORM) ready (openssl-new ${OPENSSL_NEW_VERSION}):"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
