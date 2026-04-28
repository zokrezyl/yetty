#!/bin/bash
# Builds openh264 (cisco/openh264) for $TARGET_PLATFORM and packages it as
# a tarball under $OUTPUT_DIR. Mirrors the per-platform make invocations in
# build-tools/cmake/openh264.cmake — but produces an installable bundle
# (lib/ + include/) that 3rdparty-fetch.cmake can drop into the build tree.
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
# truth for both upstream tag fetch (v<VER>) and tarball naming.
#
# Optional env:
#   WORK_DIR          default /tmp/yetty-3rdparty-openh264-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#                     holds the openh264 source tarball so multi-target
#                     builds share a single download
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

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-openh264-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Cisco tags upstream releases as `vX.Y.Z` — version file holds the bare X.Y.Z.
OPENH264_URL="https://github.com/cisco/openh264/archive/refs/tags/v${VERSION}.tar.gz"
OPENH264_TARBALL="$CACHE_DIR/openh264-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/openh264-${VERSION}-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/openh264-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Fetch (shared across targets) — flock so parallel target builds share
# a single download. Per-PID .part avoids clobbering on hosts without flock.
#-----------------------------------------------------------------------------
if [ ! -f "$OPENH264_TARBALL" ]; then
    _part="$OPENH264_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then
            flock -x 9
        fi
        if [ ! -f "$OPENH264_TARBALL" ]; then
            echo "==> downloading openh264 ${VERSION}"
            # github.com sporadically returns 502 — bump retries + delay
            # and use --retry-all-errors so any 5xx counts as retryable.
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$OPENH264_URL"
            mv "$_part" "$OPENH264_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.openh264-download.lock"
    rm -f "$_part"
else
    echo "==> using cached openh264 source: $OPENH264_TARBALL"
fi

#-----------------------------------------------------------------------------
# Extract — openh264 builds in-source, so we keep one extraction per target
# (multiple TARGET_PLATFORM builds in the same WORK_DIR don't collide).
#-----------------------------------------------------------------------------
rm -rf "$SRC_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"
echo "==> extracting -> $SRC_DIR"
mkdir -p "$WORK_DIR/.extract-${TARGET_PLATFORM}"
tar -C "$WORK_DIR/.extract-${TARGET_PLATFORM}" -xzf "$OPENH264_TARBALL"
mv "$WORK_DIR/.extract-${TARGET_PLATFORM}/openh264-${VERSION}" "$SRC_DIR"
rmdir "$WORK_DIR/.extract-${TARGET_PLATFORM}"

#-----------------------------------------------------------------------------
# Per-platform make args. openh264's top-level Makefile understands OS=,
# ARCH=, NDKROOT=, etc.; we pick the right ones here.
#-----------------------------------------------------------------------------
COMMON_ARGS=(
    BUILDTYPE=Release
    ENABLE_SHARED=No
    "PREFIX=$INSTALL_DIR"
)
EXTRA_ARGS=()
MAKE_WRAPPER=""   # "" for plain make, "emmake" for webasm

case "$TARGET_PLATFORM" in

linux-x86_64)
    EXTRA_ARGS=(ARCH=x86_64)
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    EXTRA_ARGS=(
        ARCH=arm64
        "CC=${CROSS_PREFIX}gcc"
        "CXX=${CROSS_PREFIX}g++"
        "AR=${CROSS_PREFIX}ar"
        "AS=${CROSS_PREFIX}as"
        "STRIP=${CROSS_PREFIX}strip"
    )
    ;;

macos-x86_64)
    EXTRA_ARGS=(OS=darwin ARCH=x86_64)
    ;;

macos-arm64)
    EXTRA_ARGS=(OS=darwin ARCH=arm64)
    ;;

android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _OH264_ARCH=arm64 ;;
        android-x86_64)    _OH264_ARCH=x86_64 ;;
    esac
    EXTRA_ARGS=(
        OS=android
        "ARCH=${_OH264_ARCH}"
        "NDKROOT=${ANDROID_NDK_HOME}"
        "TARGET=android-${ANDROID_API}"
        "NDKLEVEL=${ANDROID_API}"
    )
    ;;

ios-arm64|ios-x86_64)
    # The 3rdparty-ios-* nix shell on macOS pulls xcbuild's stub xcrun
    # via stdenv-darwin. That stub doesn't know about real iOS SDKs and
    # fails with `error: unable to find sdk: 'iphoneos'`.
    #
    # Two things must be done:
    #   1. Unset DEVELOPER_DIR / SDKROOT / NIX_APPLE_SDK_VERSION so even
    #      the real /usr/bin/xcrun isn't redirected to the nix SDK.
    #   2. Prepend /usr/bin to PATH so *bare* `xcrun` (used by openh264's
    #      build/platform-ios.mk via $(shell xcrun --show-sdk-path) — a
    #      shell-out we can't override via CC/CXX) resolves to Apple's
    #      xcrun, not nix's stub.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"

    : "${IOS_MIN:=15.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)  _IOS_SDK="iphoneos";        _OH264_ARCH=arm64  ;;
        ios-x86_64) _IOS_SDK="iphonesimulator"; _OH264_ARCH=x86_64 ;;
    esac
    EXTRA_ARGS=(
        OS=ios
        "ARCH=${_OH264_ARCH}"
        "SDK_MIN=${IOS_MIN}"
        "CC=/usr/bin/xcrun -sdk ${_IOS_SDK} clang"
        "CXX=/usr/bin/xcrun -sdk ${_IOS_SDK} clang++"
        "AR=/usr/bin/xcrun -sdk ${_IOS_SDK} ar"
    )
    ;;

tvos-x86_64|tvos-arm64)
    # Same xcrun trap as ios — see ios case above.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"

    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        tvos-x86_64) _OH264_ARCH=x86_64 ;;
        tvos-arm64)  _OH264_ARCH=arm64  ;;
    esac
    # openh264 v2.4.1 ships no build/platform-tvos.mk — only platform-ios.mk,
    # which hardcodes -miphoneos-version-min=$(SDK_MIN). Drop a tvos-specific
    # platform makefile alongside it; the top-level Makefile picks it up via
    # `include build/platform-$(OS).mk` when we pass OS=tvos.
    cat > "$SRC_DIR/build/platform-tvos.mk" <<'TVOS_MK'
include $(SRC_PATH)build/platform-darwin.mk
CXX = clang++
CC = clang
SDKTYPE = AppleTVSimulator
SDKROOT := $(shell xcrun --sdk $(shell echo $(SDKTYPE) | tr A-Z a-z) --show-sdk-path)
CFLAGS  += -arch $(ARCH) -isysroot $(SDKROOT) -mtvos-simulator-version-min=$(SDK_MIN) -DAPPLE_IOS
LDFLAGS += -arch $(ARCH) -isysroot $(SDKROOT) -mtvos-simulator-version-min=$(SDK_MIN)
TVOS_MK
    EXTRA_ARGS=(
        OS=tvos
        "ARCH=${_OH264_ARCH}"
        "SDK_MIN=${TVOS_MIN}"
        "CC=/usr/bin/xcrun -sdk appletvsimulator clang"
        "CXX=/usr/bin/xcrun -sdk appletvsimulator clang++"
        "AR=/usr/bin/xcrun -sdk appletvsimulator ar"
    )
    ;;

webasm)
    command -v emmake >/dev/null 2>&1 || {
        echo "error: emmake not found — source the .#3rdparty-webasm shell" >&2
        exit 1
    }
    # Match the cmake recipe: ARCH= (empty) prevents -m64 flags; -D__APPLE__
    # forces openh264 to use pthread_cond_t instead of sem_timedwait (not in
    # emscripten); USE_ASM=No because we have no NASM target for wasm.
    EXTRA_ARGS=(
        USE_ASM=No
        ARCH=
        "CFLAGS=-pthread -D__APPLE__"
        "CXXFLAGS=-pthread -D__APPLE__"
        "LDFLAGS=-pthread"
    )
    MAKE_WRAPPER="emmake"
    ;;

windows-x86_64)
    # Cisco's Makefile supports OS=msvc — invoked through GNU make
    # (chocolatey: `choco install make`) under a vcvarsall'd shell so
    # cl.exe produces MSVC-ABI openh264.lib for yetty.exe linking.
    EXTRA_ARGS=(OS=msvc ARCH=x86_64)
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Build + install. openh264 builds in-source, so cd into SRC_DIR and clear
# stamps from any prior (cancelled) run — `make clean` before each phase.
#-----------------------------------------------------------------------------
echo "==> building openh264 for $TARGET_PLATFORM (-j${NCPU})"
echo "    args: ${COMMON_ARGS[*]} ${EXTRA_ARGS[*]}"

cd "$SRC_DIR"

if [ -n "$MAKE_WRAPPER" ]; then
    "$MAKE_WRAPPER" make -j"$NCPU" "${COMMON_ARGS[@]}" "${EXTRA_ARGS[@]}" libraries
    "$MAKE_WRAPPER" make            "${COMMON_ARGS[@]}" "${EXTRA_ARGS[@]}" install-static
else
    make -j"$NCPU" "${COMMON_ARGS[@]}" "${EXTRA_ARGS[@]}" libraries
    make            "${COMMON_ARGS[@]}" "${EXTRA_ARGS[@]}" install-static
fi

#-----------------------------------------------------------------------------
# Verify install layout
#-----------------------------------------------------------------------------
case "$TARGET_PLATFORM" in
    windows-x86_64) _LIB="$INSTALL_DIR/lib/openh264.lib"     ;;
    *)              _LIB="$INSTALL_DIR/lib/libopenh264.a"    ;;
esac

if [ ! -f "$_LIB" ]; then
    echo "missing library: $_LIB" >&2
    echo "install tree:" >&2
    find "$INSTALL_DIR" -maxdepth 4 -print >&2 || true
    exit 1
fi

if [ ! -d "$INSTALL_DIR/include/wels" ]; then
    echo "missing headers: $INSTALL_DIR/include/wels/" >&2
    exit 1
fi

#-----------------------------------------------------------------------------
# Stage + package — keep the install layout (lib/, include/) so the
# 3rdparty-fetch consumer can hardcode the same paths Dawn/Dav1d use.
#-----------------------------------------------------------------------------
cp -a "$INSTALL_DIR/lib"     "$STAGE/"
cp -a "$INSTALL_DIR/include" "$STAGE/"

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "openh264 $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 20 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,20p'
