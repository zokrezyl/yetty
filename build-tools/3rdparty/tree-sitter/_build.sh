#!/bin/bash
# Builds tree-sitter (core lib + 15 grammars) for $TARGET_PLATFORM and
# packages them as a single tarball under $OUTPUT_DIR. Replaces the
# from-source build in build-tools/cmake/TreeSitter.cmake which fetched
# 16 separate tarballs at every yetty configure (frequent github 502s
# and the "downloads bunch of files and often hangs" problem).
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     macos-arm64 | macos-x86_64 |
#                     android-arm64-v8a | android-x86_64 |
#                     ios-arm64 | ios-x86_64 | tvos-x86_64 |
#                     webasm
#   OUTPUT_DIR        where the tarball is written
#
# Version (core tree-sitter) is read from this directory's `version`
# file — single source of truth for both the core fetch and tarball
# naming. Per-grammar tags are pinned in the GRAMMARS table below.
#
# Optional env:
#   WORK_DIR          default /tmp/yetty-3rdparty-tree-sitter-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-3rdparty
#   ANDROID_API       default 26
#   IOS_MIN           default 15.0
#   TVOS_MIN          default 17.0
#   CROSS_PREFIX      default aarch64-unknown-linux-gnu- (linux-aarch64)
#
# Output tarball layout (consumed by build-tools/cmake/TreeSitter.cmake):
#   lib/libtree-sitter-core.a
#   lib/libts-grammar-<name>.a   (× 15)
#   include/tree_sitter/api.h
#   queries/<name>/<*.scm>       (× 15)

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
TS_CORE_VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$TS_CORE_VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
VERSION="$TS_CORE_VERSION"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-tree-sitter-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

SRC_BASE="$WORK_DIR/sources"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/tree-sitter-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR" "$SRC_BASE"
rm -rf "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR/lib" "$INSTALL_DIR/include" "$INSTALL_DIR/queries" "$STAGE"

#-----------------------------------------------------------------------------
# Per-platform toolchain selection — same matrix as openssl/dav1d/openh264.
# Sets CC, CXX, AR, CFLAGS_EXTRA. Cross-compilation handled via env vars.
#-----------------------------------------------------------------------------
# gnu11/gnu++17 (not c11/c++17) so POSIX feature-test macros expose
# fdopen/dup/etc. without us defining _GNU_SOURCE manually. Mirrors what
# cmake's `C_STANDARD 11` + EXTENSIONS=ON (the default) emits.
CFLAGS_BASE="-O2 -fPIC -std=gnu11"
CXXFLAGS_BASE="-O2 -fPIC -std=gnu++17"
CC=cc
CXX=c++
AR=ar
CFLAGS_EXTRA=""

case "$TARGET_PLATFORM" in

linux-x86_64)
    CC=gcc; CXX=g++; AR=ar
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"
    CXX="${CROSS_PREFIX}g++"
    AR="${CROSS_PREFIX}ar"
    ;;

macos-x86_64)
    CC=clang; CXX=clang++; AR=ar
    CFLAGS_EXTRA="-arch x86_64"
    ;;

macos-arm64)
    CC=clang; CXX=clang++; AR=ar
    CFLAGS_EXTRA="-arch arm64"
    ;;

ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64)
    # nix-on-macOS xcrun trap — same as openssl/dav1d/openh264. Apple's
    # xcrun must be first on PATH; nix's apple-sdk env vars unset so
    # /usr/bin/xcrun finds the real iOS / tvOS SDK.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION
    export PATH="/usr/bin:$PATH"

    : "${IOS_MIN:=15.0}"
    : "${TVOS_MIN:=17.0}"
    case "$TARGET_PLATFORM" in
        ios-arm64)
            _IOS_SDK="iphoneos";        _IOS_ARCH="arm64"
            _MIN_FLAG="-miphoneos-version-min=${IOS_MIN}"
            ;;
        ios-x86_64)
            _IOS_SDK="iphonesimulator"; _IOS_ARCH="x86_64"
            _MIN_FLAG="-mios-simulator-version-min=${IOS_MIN}"
            ;;
        tvos-x86_64)
            _IOS_SDK="appletvsimulator"; _IOS_ARCH="x86_64"
            _MIN_FLAG="-mtvos-simulator-version-min=${TVOS_MIN}"
            ;;
        tvos-arm64)
            _IOS_SDK="appletvsimulator"; _IOS_ARCH="arm64"
            _MIN_FLAG="-mtvos-simulator-version-min=${TVOS_MIN}"
            ;;
    esac
    _IOS_SYSROOT="$(/usr/bin/xcrun --sdk "$_IOS_SDK" --show-sdk-path)"
    [ -d "$_IOS_SYSROOT" ] || { echo "missing SDK: $_IOS_SYSROOT" >&2; exit 1; }
    CC="/usr/bin/xcrun -sdk $_IOS_SDK clang"
    CXX="/usr/bin/xcrun -sdk $_IOS_SDK clang++"
    AR="/usr/bin/xcrun -sdk $_IOS_SDK ar"
    CFLAGS_EXTRA="-arch $_IOS_ARCH -isysroot $_IOS_SYSROOT $_MIN_FLAG"
    ;;

android-arm64-v8a|android-x86_64)
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#3rdparty-${TARGET_PLATFORM} shell}"
    : "${ANDROID_API:=26}"
    case "$TARGET_PLATFORM" in
        android-arm64-v8a) _NDK_TRIPLE="aarch64-linux-android" ;;
        android-x86_64)    _NDK_TRIPLE="x86_64-linux-android"  ;;
    esac
    CC="${_NDK_TRIPLE}${ANDROID_API}-clang"
    CXX="${_NDK_TRIPLE}${ANDROID_API}-clang++"
    AR="llvm-ar"
    command -v "$CC" >/dev/null 2>&1 || {
        echo "error: $CC not on PATH (NDK shellHook expected)" >&2
        exit 1
    }
    ;;

webasm)
    command -v emcc >/dev/null 2>&1 || {
        echo "error: emcc not found — source the .#3rdparty-webasm shell" >&2
        exit 1
    }
    CC=emcc
    CXX=em++
    AR=emar
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

CFLAGS="$CFLAGS_BASE $CFLAGS_EXTRA"
CXXFLAGS="$CXXFLAGS_BASE $CFLAGS_EXTRA"

#-----------------------------------------------------------------------------
# Fetch + extract any of the 16 GitHub source tarballs into $SRC_BASE.
# Cached across targets — same source feeds every target build.
#-----------------------------------------------------------------------------
_fetch() {
    local cache_name="$1" url="$2"
    local cache="$CACHE_DIR/$cache_name"
    if [ ! -f "$cache" ]; then
        local part="$cache.part.$$"
        (
            if command -v flock >/dev/null 2>&1; then flock -x 9; fi
            if [ ! -f "$cache" ]; then
                echo "==> fetching $cache_name"
                curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                    -o "$part" "$url"
                mv "$part" "$cache"
            fi
        ) 9>"$CACHE_DIR/.tree-sitter-download.lock"
        rm -f "$part"
    fi
}

_extract() {
    local cache_name="$1" extract_to="$2"
    if [ ! -d "$extract_to" ]; then
        mkdir -p "$extract_to"
        tar -C "$extract_to" --strip-components=1 -xzf "$CACHE_DIR/$cache_name"
    fi
}

#-----------------------------------------------------------------------------
# Core tree-sitter library
#-----------------------------------------------------------------------------
TS_CORE_TARBALL="tree-sitter-core-${TS_CORE_VERSION}.tar.gz"
_fetch "$TS_CORE_TARBALL" "https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v${TS_CORE_VERSION}.tar.gz"
TS_CORE_DIR="$SRC_BASE/tree-sitter-core"
_extract "$TS_CORE_TARBALL" "$TS_CORE_DIR"

echo "==> compiling tree-sitter core for $TARGET_PLATFORM"
$CC $CFLAGS \
    -I"$TS_CORE_DIR/lib/include" \
    -I"$TS_CORE_DIR/lib/src" \
    -c "$TS_CORE_DIR/lib/src/lib.c" \
    -o "$WORK_DIR/tree-sitter-core.o"
$AR rcs "$INSTALL_DIR/lib/libtree-sitter-core.a" "$WORK_DIR/tree-sitter-core.o"
cp -a "$TS_CORE_DIR/lib/include/tree_sitter" "$INSTALL_DIR/include/"

#-----------------------------------------------------------------------------
# Grammars — name|repo|tag|scanner|src_subdir|queries_subdir|queries_dir
#   scanner = 1 if scanner.c/.cc exists and should be linked
#   src_subdir = parser.c lives at <repo>/<src_subdir>/src/parser.c (default: empty)
#   queries_subdir = source queries dir is <repo>/queries/<queries_subdir>/
#   queries_dir = override; full path under <repo>/ (used by markdown)
# Source-of-truth mirrors build-tools/cmake/TreeSitter.cmake's add_ts_grammar
# calls; bump per-grammar tags here when bumping the bundle.
#-----------------------------------------------------------------------------
GRAMMARS=(
    "c|tree-sitter/tree-sitter-c|v0.24.1|0|||"
    "cpp|tree-sitter/tree-sitter-cpp|v0.23.4|1|||"
    "python|tree-sitter/tree-sitter-python|v0.25.0|1|||"
    "javascript|tree-sitter/tree-sitter-javascript|v0.25.0|1|||"
    "typescript|tree-sitter/tree-sitter-typescript|v0.23.2|1|typescript||"
    "rust|tree-sitter/tree-sitter-rust|v0.24.0|1|||"
    "go|tree-sitter/tree-sitter-go|v0.25.0|0|||"
    "java|tree-sitter/tree-sitter-java|v0.23.5|0|||"
    "bash|tree-sitter/tree-sitter-bash|v0.25.1|1|||"
    "json|tree-sitter/tree-sitter-json|v0.24.8|0|||"
    "yaml|tree-sitter-grammars/tree-sitter-yaml|v0.7.2|1|||"
    "toml|tree-sitter-grammars/tree-sitter-toml|v0.7.0|1|||"
    "html|tree-sitter/tree-sitter-html|v0.23.2|1|||"
    "xml|tree-sitter-grammars/tree-sitter-xml|v0.7.0|1|xml|xml|"
    "markdown|tree-sitter-grammars/tree-sitter-markdown|v0.4.1|1|tree-sitter-markdown||tree-sitter-markdown/queries"
)

for record in "${GRAMMARS[@]}"; do
    IFS='|' read -r G_NAME G_REPO G_TAG G_SCANNER G_SUBDIR G_QUERIES_SUBDIR G_QUERIES_DIR <<< "$record"

    echo "==> grammar $G_NAME ($G_TAG)"
    G_CACHE_NAME="tree-sitter-${G_NAME}-${G_TAG}.tar.gz"
    G_REPO_DIR="$SRC_BASE/tree-sitter-${G_NAME}"
    _fetch  "$G_CACHE_NAME" "https://github.com/${G_REPO}/archive/refs/tags/${G_TAG}.tar.gz"
    _extract "$G_CACHE_NAME" "$G_REPO_DIR"

    if [ -n "$G_SUBDIR" ]; then
        G_SRC_DIR="$G_REPO_DIR/$G_SUBDIR/src"
    else
        G_SRC_DIR="$G_REPO_DIR/src"
    fi

    OBJS=()
    [ -f "$G_SRC_DIR/parser.c" ] || { echo "missing $G_SRC_DIR/parser.c" >&2; exit 1; }
    $CC $CFLAGS \
        -I"$G_SRC_DIR" \
        -I"$TS_CORE_DIR/lib/include" \
        -c "$G_SRC_DIR/parser.c" \
        -o "$WORK_DIR/${G_NAME}-parser.o"
    OBJS+=("$WORK_DIR/${G_NAME}-parser.o")

    if [ "$G_SCANNER" = "1" ]; then
        if [ -f "$G_SRC_DIR/scanner.c" ]; then
            $CC $CFLAGS \
                -I"$G_SRC_DIR" \
                -I"$TS_CORE_DIR/lib/include" \
                -c "$G_SRC_DIR/scanner.c" \
                -o "$WORK_DIR/${G_NAME}-scanner.o"
            OBJS+=("$WORK_DIR/${G_NAME}-scanner.o")
        elif [ -f "$G_SRC_DIR/scanner.cc" ]; then
            $CXX $CXXFLAGS \
                -I"$G_SRC_DIR" \
                -I"$TS_CORE_DIR/lib/include" \
                -c "$G_SRC_DIR/scanner.cc" \
                -o "$WORK_DIR/${G_NAME}-scanner.o"
            OBJS+=("$WORK_DIR/${G_NAME}-scanner.o")
        fi
    fi

    $AR rcs "$INSTALL_DIR/lib/libts-grammar-${G_NAME}.a" "${OBJS[@]}"

    # Stage queries: pick source layout, copy into queries/<name>/.
    if [ -n "$G_QUERIES_DIR" ]; then
        Q_SRC="$G_REPO_DIR/$G_QUERIES_DIR"
    elif [ -n "$G_QUERIES_SUBDIR" ]; then
        Q_SRC="$G_REPO_DIR/queries/$G_QUERIES_SUBDIR"
    else
        Q_SRC="$G_REPO_DIR/queries"
    fi
    if [ -d "$Q_SRC" ]; then
        mkdir -p "$INSTALL_DIR/queries/${G_NAME}"
        cp -a "$Q_SRC/." "$INSTALL_DIR/queries/${G_NAME}/"
    else
        echo "    (no queries dir: $Q_SRC — skipping)"
    fi
done

#-----------------------------------------------------------------------------
# Stage + verify
#-----------------------------------------------------------------------------
cp -a "$INSTALL_DIR/lib"     "$STAGE/"
cp -a "$INSTALL_DIR/include" "$STAGE/"
cp -a "$INSTALL_DIR/queries" "$STAGE/"

# Sanity: every expected lib produced
for record in "${GRAMMARS[@]}"; do
    IFS='|' read -r G_NAME _ <<< "$record"
    if [ ! -f "$STAGE/lib/libts-grammar-${G_NAME}.a" ]; then
        echo "missing libts-grammar-${G_NAME}.a in stage" >&2
        exit 1
    fi
done
[ -f "$STAGE/lib/libtree-sitter-core.a" ] || { echo "missing libtree-sitter-core.a" >&2; exit 1; }
[ -f "$STAGE/include/tree_sitter/api.h" ]  || { echo "missing tree_sitter/api.h" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "tree-sitter $VERSION + ${#GRAMMARS[@]} grammars ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 30 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,30p'
