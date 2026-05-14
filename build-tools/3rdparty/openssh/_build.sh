#!/bin/bash
# Builds openssh-portable for $TARGET_PLATFORM using upstream ./configure + make.
# Links against the prebuilt openssl-1.1.1w tarball — openssh 9.8p1 only
# accepts OpenSSL 1.x/3.x (SSH doesn't use TLS; the OpenSSL link provides
# crypto primitives, not a TLS layer).
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | linux-riscv64 |
#                     macos-arm64  | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Optional env:
#   YETTY_3RDPARTY_URL_BASE    default https://github.com/zokrezyl/yetty/releases/download
#   OPENSSL_VERSION_OVERRIDE   pin a different openssl version
#                               (default: read from build-tools/3rdparty/openssl/version)
#   WORK_DIR          default /tmp/yetty-3rdparty-openssh-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#   CROSS_PREFIX      cross-compiler prefix (linux-aarch64, linux-riscv64)
#   ANDROID_NDK_HOME  required for android targets
#   ANDROID_API       default 26
#
# Output tarball layout (consumed by build-tools/yetty/openssh.cmake):
#   bin/ssh

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

# OpenSSL version — reads the 1.1.1w pin; openssh 9.x rejects OpenSSL 4.x.
OSSL_VERSION_FILE="$REPO_ROOT/build-tools/3rdparty/openssl/version"
: "${OPENSSL_VERSION_OVERRIDE:=}"
if [ -n "$OPENSSL_VERSION_OVERRIDE" ]; then
    OSSL_VERSION="$OPENSSL_VERSION_OVERRIDE"
else
    [ -f "$OSSL_VERSION_FILE" ] || { echo "missing $OSSL_VERSION_FILE" >&2; exit 1; }
    OSSL_VERSION="$(tr -d '[:space:]' < "$OSSL_VERSION_FILE")"
fi
[ -n "$OSSL_VERSION" ] || { echo "openssl version unresolved" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-openssh-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"

OPENSSH_URL="https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-${VERSION}.tar.gz"
OPENSSH_TARBALL="$CACHE_DIR/openssh-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/openssh-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/openssh-${TARGET_PLATFORM}-${VERSION}.tar.gz"

# Prebuilt openssl tarball (1.1.1w, the version openssh 9.x supports).
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
# Fetch openssh source + prebuilt openssl tarball.
#-----------------------------------------------------------------------------
fetch "$OPENSSH_URL"  "$OPENSSH_TARBALL" "openssh ${VERSION}"                           openssh-source
fetch "$OSSL_TAR_URL" "$OSSL_TARBALL"    "openssl ${OSSL_VERSION} (${TARGET_PLATFORM})" openssh-openssl

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting openssh -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$OPENSSH_TARBALL"
fi

echo "==> extracting prebuilt openssl -> $OSSL_PREFIX"
rm -rf "$OSSL_PREFIX"
mkdir -p "$OSSL_PREFIX"
tar -C "$OSSL_PREFIX" -xzf "$OSSL_TARBALL"

[ -f "$OSSL_PREFIX/lib/libssl.a" ]    || { echo "missing libssl.a in $OSSL_PREFIX/lib/" >&2; exit 1; }
[ -f "$OSSL_PREFIX/lib/libcrypto.a" ] || { echo "missing libcrypto.a in $OSSL_PREFIX/lib/" >&2; exit 1; }
[ -d "$OSSL_PREFIX/include/openssl" ] || { echo "missing $OSSL_PREFIX/include/openssl/" >&2; exit 1; }

rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$BUILD_DIR" "$INSTALL_DIR" "$STAGE/bin"

#-----------------------------------------------------------------------------
# Per-platform configure args.
#-----------------------------------------------------------------------------
CONFIGURE_ARGS=(
    --prefix="$INSTALL_DIR"
    --with-ssl-dir="$OSSL_PREFIX"
    --without-zlib
    --without-pam
    --without-selinux
    --disable-strip
    --without-rpath
)

HOST_ARG=()

case "$TARGET_PLATFORM" in

linux-x86_64)
    : # native gcc
    ;;

linux-aarch64)
    # Use = (not :=) so an explicit CROSS_PREFIX="" from the native arm
    # runner is preserved; := would override empty with the default.
    : "${CROSS_PREFIX=aarch64-unknown-linux-gnu-}"
    if [ -n "$CROSS_PREFIX" ]; then
        HOST_ARG=(--host=aarch64-linux-gnu)
        export CC="${CROSS_PREFIX}gcc"
        export AR="${CROSS_PREFIX}ar"
        export RANLIB="${CROSS_PREFIX}ranlib"
        export LD="${CROSS_PREFIX}ld"
        # Pre-answer AC_TRY_RUN checks that can't run on the host.
        export ac_cv_func_getaddrinfo=yes
        export ac_cv_have_accrights_in_msghdr=no
        export ac_cv_have_control_in_msghdr=yes
        export ac_cv_func_poll=yes
    fi
    ;;

linux-riscv64)
    : "${CROSS_PREFIX=riscv64-unknown-linux-gnu-}"
    HOST_ARG=(--host=riscv64-linux-gnu)
    export CC="${CROSS_PREFIX}gcc"
    export AR="${CROSS_PREFIX}ar"
    export RANLIB="${CROSS_PREFIX}ranlib"
    export LD="${CROSS_PREFIX}ld"
    export ac_cv_func_getaddrinfo=yes
    export ac_cv_have_accrights_in_msghdr=no
    export ac_cv_have_control_in_msghdr=yes
    export ac_cv_func_poll=yes
    ;;

macos-arm64)
    : # native clang on macos-latest (arm64)
    ;;

macos-x86_64)
    : # native clang on macos-15-intel
    ;;

android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a)
            _ANDROID_TRIPLE="aarch64-linux-android"
            _HOST_TRIPLE="aarch64-linux-android"
            ;;
        android-x86_64)
            _ANDROID_TRIPLE="x86_64-linux-android"
            _HOST_TRIPLE="x86_64-linux-android"
            ;;
    esac
    _NDK_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
    export CC="$_NDK_BIN/${_ANDROID_TRIPLE}${ANDROID_API}-clang"
    export AR="$_NDK_BIN/llvm-ar"
    export RANLIB="$_NDK_BIN/llvm-ranlib"
    export LD="$_NDK_BIN/ld"
    HOST_ARG=(--host="$_HOST_TRIPLE")
    CONFIGURE_ARGS+=(--without-privsep-user --without-privsep-path)
    export ac_cv_func_getaddrinfo=yes
    export ac_cv_have_accrights_in_msghdr=no
    export ac_cv_have_control_in_msghdr=yes
    export ac_cv_func_poll=yes
    ;;

*)
    echo "unsupported TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Configure + build ssh client only.
#-----------------------------------------------------------------------------
echo "==> configuring openssh ${VERSION} for ${TARGET_PLATFORM} (openssl ${OSSL_VERSION})"
(
    cd "$BUILD_DIR"
    "$SRC_DIR/configure" "${HOST_ARG[@]}" "${CONFIGURE_ARGS[@]}"
)

echo "==> building ssh client (-j${NCPU})"
make -C "$BUILD_DIR" -j"$NCPU" ssh

echo "==> staging"
cp "$BUILD_DIR/ssh" "$STAGE/bin/ssh"
chmod 0755 "$STAGE/bin/ssh"

[ -x "$STAGE/bin/ssh" ] || { echo "missing or non-executable $STAGE/bin/ssh" >&2; exit 1; }

# Sanity: no shared libssl/libcrypto dependency.
if command -v ldd >/dev/null 2>&1; then
    if ldd "$STAGE/bin/ssh" 2>/dev/null | grep -q 'libssl\.so\|libcrypto\.so'; then
        echo "warning: ssh binary links against shared libssl/libcrypto" >&2
    fi
elif command -v otool >/dev/null 2>&1; then
    if otool -L "$STAGE/bin/ssh" 2>/dev/null | grep -q 'libssl\|libcrypto'; then
        echo "warning: ssh binary links against shared libssl/libcrypto" >&2
    fi
fi

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "openssh ${VERSION} (${TARGET_PLATFORM}, openssl ${OSSL_VERSION}) ready:"
ls -lh "$TARBALL"
echo "contents:"
tar -tzf "$TARBALL"
