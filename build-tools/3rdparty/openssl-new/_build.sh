#!/bin/bash
# Builds upstream OpenSSL ${VERSION} (openssl/openssl) for $TARGET_PLATFORM
# using OpenSSL's native ./Configure + make build. Replaces the previous
# janbar/openssl-cmake wrapper — that wrapper was stuck on 1.1.1w (EOL'd
# 2023-09-11) with no security backports tracked.
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
# truth for both upstream tag fetch (`openssl-<VER>` on the openssl repo)
# and tarball naming.
#
# Optional env:
#   WORK_DIR          default /tmp/yetty-3rdparty-openssl-new-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#   ANDROID_API       default 26
#   IOS_MIN           default 15.0
#   TVOS_MIN          default 17.0
#   CROSS_PREFIX      default aarch64-unknown-linux-gnu-

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

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-openssl-new-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Upstream tag is `openssl-<VER>` (e.g. `openssl-4.0.0`); GitHub archive
# top-level dir is `openssl-openssl-<VER>` (repo name + tag).
OPENSSL_URL="https://github.com/openssl/openssl/archive/refs/tags/openssl-${VERSION}.tar.gz"
OPENSSL_TARBALL="$CACHE_DIR/openssl-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/openssl-${VERSION}-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/openssl-new-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch (shared across targets) — flock + per-PID .part across hosts
# without flock. github.com sporadic 502s — generous retries.
#-----------------------------------------------------------------------------
if [ ! -f "$OPENSSL_TARBALL" ]; then
    _part="$OPENSSL_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then
            flock -x 9
        fi
        if [ ! -f "$OPENSSL_TARBALL" ]; then
            echo "==> downloading openssl/openssl ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$OPENSSL_URL"
            mv "$_part" "$OPENSSL_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.openssl-download.lock"
    rm -f "$_part"
else
    echo "==> using cached openssl source: $OPENSSL_TARBALL"
fi

#-----------------------------------------------------------------------------
# Extract — openssl's native build is in-source (Configure writes
# Makefile + headers next to the sources). One extraction per target so
# multiple TARGET_PLATFORM builds in the same WORK_DIR don't collide.
#-----------------------------------------------------------------------------
if [ ! -d "$SRC_DIR" ]; then
    rm -rf "$WORK_DIR/.extract-${TARGET_PLATFORM}"
    mkdir -p "$WORK_DIR/.extract-${TARGET_PLATFORM}"
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR/.extract-${TARGET_PLATFORM}" -xzf "$OPENSSL_TARBALL"
    mv "$WORK_DIR/.extract-${TARGET_PLATFORM}/openssl-openssl-${VERSION}" "$SRC_DIR"
    rmdir "$WORK_DIR/.extract-${TARGET_PLATFORM}"
fi
rm -rf "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

#-----------------------------------------------------------------------------
# Configure args — common across all targets. Static-only, no apps,
# no docs, no tests. --libdir=lib forces lib/ (not lib64/) so the stage
# layout is uniform across distros.
#-----------------------------------------------------------------------------
CFG_ARGS=(
    --prefix="$INSTALL_DIR"
    --libdir=lib
    no-shared
    no-tests
    no-apps
    no-docs
    no-makedepend
    # SM2/SM3/SM4 are Chinese national crypto algos — yetty's TLS path
    # (libcurl/libssh2/cpr to standard CAs) never touches them. Disabling
    # also dodges crypto/sm4/sm4-x86_64.S using Intel SM4-NI instructions
    # (vsm4key4, vsm3msg1, vsm3rnds2) that older NDK clang assemblers
    # reject (Android x86_64 build dies on these otherwise).
    no-sm2
    no-sm3
    no-sm4
)
MAKE_CMD="make"
MAKE_PREFIX=""    # "emmake " for webasm

case "$TARGET_PLATFORM" in

linux-x86_64)
    CFG_TARGET="linux-x86_64"
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CFG_TARGET="linux-aarch64"
    export CC="${CROSS_PREFIX}gcc"
    export CXX="${CROSS_PREFIX}g++"
    export AR="${CROSS_PREFIX}ar"
    export RANLIB="${CROSS_PREFIX}ranlib"
    ;;

macos-x86_64)
    CFG_TARGET="darwin64-x86_64-cc"
    ;;

macos-arm64)
    CFG_TARGET="darwin64-arm64-cc"
    ;;

ios-arm64|ios-x86_64)
    # nix-on-macOS xcrun trap — same as openh264/dav1d. Apple's xcrun
    # must be first on PATH; nix's apple-sdk env vars unset so /usr/bin/xcrun
    # finds the real iOS SDK.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"

    : "${IOS_MIN:=15.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)
            # ios64-xcrun: openssl's Configurations/15-ios.conf target that
            # invokes `xcrun -sdk iphoneos clang` internally for SDK lookup.
            CFG_TARGET="ios64-xcrun"
            ;;
        ios-x86_64)
            CFG_TARGET="iossimulator-xcrun"
            ;;
    esac
    CFG_ARGS+=("-mios-version-min=${IOS_MIN}")
    ;;

tvos-x86_64)
    # Same xcrun trap as ios — see above.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"

    : "${TVOS_MIN:=17.0}"
    # OpenSSL 4.0 has no built-in tvOS target. Reuse iossimulator-x86_64-xcrun
    # as the base — its `-arch x86_64 -fno-common` cflags + macosx asm scheme
    # are correct for the appletvsimulator x86_64 ABI — and override CC via
    # env to point at the tvOS simulator SDK. sys_id stays "iOS" (cosmetic
    # string baked into the lib's version output, no binary effect).
    CFG_TARGET="iossimulator-x86_64-xcrun"
    export CC="xcrun -sdk appletvsimulator cc"
    CFG_ARGS+=("-mtvos-simulator-version-min=${TVOS_MIN}")
    ;;

android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
    : "${ANDROID_API:=26}"
    # OpenSSL's Configurations/15-android.conf reads ANDROID_NDK_ROOT.
    export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) CFG_TARGET="android-arm64"  ;;
        android-x86_64)    CFG_TARGET="android-x86_64" ;;
    esac
    CFG_ARGS+=("-D__ANDROID_API__=${ANDROID_API}")
    ;;

webasm)
    command -v emcc >/dev/null 2>&1 || {
        echo "error: emcc not found — source the .#3rdparty-webasm shell" >&2
        exit 1
    }
    # No official emscripten target. Use linux-generic32 with the toolchain
    # overridden to emcc + extra disables (no-asm: no native asm; no-async:
    # avoids setjmp; no-threads: pthread support is fragile under emscripten;
    # no-engine/no-dso: no dynamic loading).
    export CC=emcc
    export AR=emar
    export RANLIB=emranlib
    CFG_TARGET="linux-generic32"
    CFG_ARGS+=(no-asm no-async no-threads no-engine no-dso no-srp)
    MAKE_PREFIX="emmake "
    ;;

windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. Use Strawberry
    # Perl: openssl's Configure pulls Locale::Maketext::Simple via Params/Check
    # which Git Bash's bundled perl is missing. Strawberry is preinstalled
    # on the windows-latest runner and on misi's dev machine.
    CFG_TARGET="VC-WIN64A"
    MAKE_CMD="nmake"
    PERL=/c/Strawberry/perl/bin/perl.exe
    [ -x "$PERL" ] || PERL=$(command -v perl)
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Configure + build + install (in-source build).
#-----------------------------------------------------------------------------
cd "$SRC_DIR"

echo "==> configuring openssl ${VERSION} for ${CFG_TARGET}"
echo "    args: ${CFG_ARGS[*]}"
"${PERL:-perl}" ./Configure "$CFG_TARGET" "${CFG_ARGS[@]}"

# nmake doesn't take -j (single-threaded by design); GNU make + emmake do.
if [ "$MAKE_CMD" = "nmake" ]; then
    _MAKE_J=()
else
    _MAKE_J=(-j"$NCPU")
fi

echo "==> building (${_MAKE_J[*]:-serial})"
${MAKE_PREFIX}$MAKE_CMD "${_MAKE_J[@]}" build_libs

echo "==> installing libs + headers (no docs/man)"
${MAKE_PREFIX}$MAKE_CMD install_dev

#-----------------------------------------------------------------------------
# Stage + verify. Modern openssl produces libssl.a + libcrypto.a directly;
# normalise from lib64/ if the install picked that path.
#-----------------------------------------------------------------------------
mkdir -p "$STAGE/lib"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        cp -a "$INSTALL_DIR/$_D/." "$STAGE/lib/"
    fi
done
cp -a "$INSTALL_DIR/include" "$STAGE/"

if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    _LIBS=("libssl.lib" "libcrypto.lib")
else
    _LIBS=("libssl.a" "libcrypto.a")
fi

for _LIB in "${_LIBS[@]}"; do
    if [ ! -f "$STAGE/lib/$_LIB" ]; then
        echo "missing library: $STAGE/lib/$_LIB" >&2
        echo "stage tree:" >&2
        find "$STAGE" -maxdepth 4 -print >&2 || true
        exit 1
    fi
done
if [ ! -f "$STAGE/include/openssl/ssl.h" ]; then
    echo "missing headers: $STAGE/include/openssl/" >&2
    exit 1
fi

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "openssl $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 20 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,20p'
