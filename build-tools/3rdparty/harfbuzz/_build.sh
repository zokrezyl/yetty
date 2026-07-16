#!/bin/bash
# Builds harfbuzz (harfbuzz/harfbuzz) for $TARGET_PLATFORM via its upstream
# CMake. Minimal static shaper: every optional dependency is disabled
# (glib/ICU/Graphite2/gobject and the platform shapers CoreText/DirectWrite/
# GDI/Uniscribe), leaving HarfBuzz's built-in hb-ucd Unicode tables. FreeType
# glue is disabled too — yetty feeds font tables from the already-loaded
# FreeType faces at runtime (hb_face over the SFNT blob), so there is no
# build-order cycle between the two prebuilt tarballs. The subsetter and the
# CLI utils (which need cairo/glib) are off.
#
# Output tarball layout (consumed by build-tools/yetty/libs/harfbuzz.cmake):
#   lib/libharfbuzz.a
#   include/harfbuzz/...         (matches upstream install)

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
# The full VERSION (may carry a packaging revision, e.g. 8.5.0-1) names the
# tarball + release tag; the upstream source archive uses only the component
# before the first dash. Same convention as freetype/libssh2/mimalloc.
UPSTREAM_VERSION="${VERSION%%-*}"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-harfbuzz-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Upstream ships a release tarball with the pre-generated (Ragel) sources, so
# no Ragel/gtk-doc toolchain is needed to build from it.
URL="https://github.com/harfbuzz/harfbuzz/releases/download/${UPSTREAM_VERSION}/harfbuzz-${UPSTREAM_VERSION}.tar.xz"
TARBALL_CACHE="$CACHE_DIR/harfbuzz-${UPSTREAM_VERSION}.tar.xz"
SRC_DIR="$WORK_DIR/harfbuzz-${UPSTREAM_VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/harfbuzz-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

fetch() {
    local url="$1" cache="$2" descr="$3" lock="$4"
    if [ ! -f "$cache" ]; then
        local part="$cache.part.$$"
        (
            if command -v flock >/dev/null 2>&1; then flock -x 9; fi
            if [ ! -f "$cache" ]; then
                echo "==> downloading $descr"
                curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$part" "$url"
                mv "$part" "$cache"
            fi
        ) 9>"$CACHE_DIR/.$lock.lock"
        rm -f "$part"
    fi
}

fetch "$URL" "$TARBALL_CACHE" "harfbuzz ${UPSTREAM_VERSION}" harfbuzz-source

if [ ! -d "$SRC_DIR" ]; then tar -C "$WORK_DIR" -xf "$TARBALL_CACHE"; fi

rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DHB_HAVE_FREETYPE=OFF
    -DHB_HAVE_GLIB=OFF
    -DHB_HAVE_GOBJECT=OFF
    -DHB_HAVE_ICU=OFF
    -DHB_HAVE_GRAPHITE2=OFF
    -DHB_HAVE_CORETEXT=OFF
    -DHB_HAVE_DIRECTWRITE=OFF
    -DHB_HAVE_UNISCRIBE=OFF
    -DHB_HAVE_GDI=OFF
    -DHB_BUILD_SUBSET=OFF
    -DHB_BUILD_UTILS=OFF
    -DHB_BUILD_TESTS=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
EMCMAKE_PREFIX=""

case "$TARGET_PLATFORM" in
linux-x86_64) : ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
    ) ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-unknown-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=riscv64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
    ) ;;
macos-x86_64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
macos-arm64)  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")  ;;
ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64)
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
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=$_CMAKE_SYS"
        "-DCMAKE_OSX_ARCHITECTURES=$_IOS_ARCH"
        "-DCMAKE_OSX_SYSROOT=$_IOS_SYSROOT"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=$_CMAKE_DEPL"
        "-DCMAKE_C_COMPILER=/usr/bin/clang"
        "-DCMAKE_CXX_COMPILER=/usr/bin/clang++"
    ) ;;
android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _ABI=arm64-v8a ;;
        android-x86_64)    _ABI=x86_64    ;;
    esac
    # openh264 already ships its C++ to android with c++_static; match it.
    CMAKE_ARGS+=(
        "-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake"
        "-DANDROID_ABI=${_ABI}"
        "-DANDROID_PLATFORM=android-${ANDROID_API}"
        "-DANDROID_STL=c++_static"
    ) ;;
webasm|webasm-mt)
    # HarfBuzz is pure compute (no threads, no POSIX file I/O), so it patterns
    # after FreeType (which builds for wasm) rather than the rejected C++
    # prebuilts. The non-mt variant links into the single-threaded yetty.wasm.
    EMCMAKE_PREFIX="emcmake"
    if [ "$TARGET_PLATFORM" = "webasm-mt" ]; then
        CMAKE_ARGS+=("-DCMAKE_C_FLAGS=-pthread" "-DCMAKE_CXX_FLAGS=-pthread")
    fi
    ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell. cmake auto-
    # detects cl.exe.
    : # cmake's default Ninja+cl pickup is fine
    ;;
*) echo "unknown $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring harfbuzz ${VERSION} for $TARGET_PLATFORM"
$EMCMAKE_PREFIX cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$NCPU"
cmake --install "$BUILD_DIR"

mkdir -p "$STAGE/lib" "$STAGE/include"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        find "$INSTALL_DIR/$_D" -maxdepth 1 \
            \( -name 'libharfbuzz*.a' -o -name 'harfbuzz*.lib' \) \
            -exec cp -a {} "$STAGE/lib/" \;
    fi
done
cp -a "$INSTALL_DIR/include/." "$STAGE/include/"

if [ ! -f "$STAGE/lib/libharfbuzz.a" ] && [ ! -f "$STAGE/lib/harfbuzz.lib" ]; then
    echo "missing libharfbuzz.a / harfbuzz.lib" >&2
    find "$INSTALL_DIR" >&2
    exit 1
fi
[ -f "$STAGE/include/harfbuzz/hb.h" ] || { echo "missing harfbuzz/hb.h" >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "harfbuzz $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
echo "contents (first 30):"
{ tar -tzf "$TARBALL" | head -30; } || true
