#!/bin/bash
# Builds NetSurf libcss (MIT) and its two MIT-licensed deps —
# libparserutils + libwapcaplet — standalone, without dragging in
# NetSurf's GPL'd browser core. Output is a self-contained prebuilt
# tarball consumed by build-tools/cmake/libs/libcss.cmake.
#
# The companion build-tools/3rdparty/netsurf/ package still exists and
# pulls these same three libs in alongside the GPL core for the legacy
# ynetsurf path. This package is what yetty will keep once netsurf is
# dropped — it isolates the MIT licensed CSS cascade from the rest.
#
# Output tarball layout:
#   lib/libcss.a
#   lib/libparserutils.a
#   lib/libwapcaplet.a
#   include/libcss/...
#   include/libwapcaplet/...
#   include/parserutils/...
#
# Build approach: fetch the three NetSurf "libs/releases" source tarballs
# + the NetSurf buildsystem-1.10 (also MIT) and drive `make install`
# with COMPONENT_TYPE=lib-static so only the static .a is produced.
# Same CC/HOST/AR cross-compile pattern netsurf/_build.sh uses; deps are
# built in order (wapcaplet → parserutils → libcss), each pointing at
# the previous via PKG_CONFIG_PATH inside a single INSTALL prefix.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "$SCRIPT_DIR/version is empty" >&2; exit 1; }

# Component versions. The libcss version is the package version (in
# `version`); the others are pinned here so the file system of truth
# stays a single semver. Bump alongside libcss when upstream rolls.
#
# The package version may carry a local patch level (`0.9.2-p1`) — the
# upstream source fetch strips it; the patches under patches/ are what
# the suffix accounts for.
BUILDSYSTEM_VERSION="1.10"
WAPCAPLET_VERSION="0.4.3"
PARSERUTILS_VERSION="0.2.5"
LIBCSS_VERSION="${VERSION%%-p*}"

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libcss-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

UPSTREAM_BASE="https://download.netsurf-browser.org/libs/releases"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libcss-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Per-platform setup. Same shape as netsurf/_build.sh — NetSurf's
# buildsystem.mk respects HOST/CC/AR overrides identical to the monkey
# target build, so cross-compile uses the same env mechanics.
#-----------------------------------------------------------------------------
NS_HOST=""
NS_CC=""
NS_AR=""
NS_NATIVE=0
NS_EXTRA_CFLAGS=""
NS_EXTRA_LDFLAGS=""

case "$TARGET_PLATFORM" in

linux-x86_64)
    NS_NATIVE=1
    ;;

linux-aarch64)
    # Native build on a GitHub-hosted ubuntu-24.04-arm runner. No cross
    # toolchain needed — stock /usr/bin/gcc + flex/bison/gperf from apt are
    # what NetSurf's buildsystem expects.
    if [ "$(uname -m)" != "aarch64" ]; then
        echo "libcss linux-aarch64 must build natively on an aarch64 host" >&2
        echo "  (got uname -m = $(uname -m))" >&2
        exit 1
    fi
    NS_NATIVE=1
    ;;

linux-riscv64)
    # Cross-compile from ubuntu-latest (x86_64) via apt's
    # gcc-riscv64-linux-gnu. No native riscv64 runner exists, so this is
    # the only linux target that cross-compiles. NetSurf's buildsystem
    # takes HOST/CC/AR overrides; we set them explicitly here.
    NS_HOST="riscv64-linux-gnu"
    NS_CC="riscv64-linux-gnu-gcc"
    NS_AR="riscv64-linux-gnu-ar"
    ;;

macos-x86_64)
    NS_NATIVE=1
    # _DARWIN_C_SOURCE bumps __DARWIN_C_LEVEL to __DARWIN_C_FULL so
    # BSD-only declarations (strcasecmp etc.) resolve in C99-strict
    # mode. -Wno-error=implicit-function-declaration is belt+braces
    # for the rare upstream missing-#include.
    NS_EXTRA_CFLAGS="-arch x86_64 -D_DARWIN_C_SOURCE -Wno-error=implicit-function-declaration"
    # macOS ships iconv as a separate libiconv (libparserutils calls
    # iconv_open/close in src/input/filter.c).
    NS_EXTRA_LDFLAGS="-arch x86_64 -liconv"
    ;;

macos-arm64)
    NS_NATIVE=1
    NS_EXTRA_CFLAGS="-arch arm64 -D_DARWIN_C_SOURCE -Wno-error=implicit-function-declaration"
    NS_EXTRA_LDFLAGS="-arch arm64 -liconv"
    ;;

android-arm64-v8a|android-x86_64|\
ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64|\
webasm|windows-x86_64)
    # These targets emit a placeholder UNSUPPORTED tarball: NetSurf's
    # buildsystem is a GNU-make build that doesn't honour CC/HOST cleanly
    # for these triples (android NDK, ios/tvos sysroot, emcc, MSVC).
    # linux-aarch64 used to be in this bucket too (cross-from-x86_64);
    # now it builds natively on ubuntu-24.04-arm — see the linux-aarch64
    # case above. linux-riscv64 cross-compiles from x86_64 — see above.
    #
    # The consumer-side build-tools/yetty/libs/libcss.cmake detects this
    # UNSUPPORTED marker and skips libcss silently — ybrowser falls back
    # to its built-in lexbor-CSS path on these platforms.
    echo "libcss: $TARGET_PLATFORM is not built (see UNSUPPORTED in tarball)" >&2
    rm -rf "$STAGE"
    mkdir -p "$STAGE"
    cat > "$STAGE/UNSUPPORTED" <<EOF
libcss $VERSION is not built for $TARGET_PLATFORM.

See build-tools/3rdparty/libcss/_build.sh for why each platform is in
the placeholder bucket. consumer-side libcss.cmake detects this marker
and skips silently — ybrowser falls back to the lexbor-CSS path.
EOF
    tar -C "$STAGE" -czf "$TARBALL" .
    echo "libcss $VERSION ($TARGET_PLATFORM) — placeholder tarball written:"
    ls -lh "$TARBALL"
    exit 0
    ;;

*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

#-----------------------------------------------------------------------------
# fetch_and_extract <name> <version> <suffix>
# Downloads $UPSTREAM_BASE/<name>-<version><suffix> into the cache,
# extracts under $WORK_DIR. Lock-guarded so parallel build matrices
# don't race on the same cache file.
#-----------------------------------------------------------------------------
fetch_and_extract() {
    local _name="$1" _ver="$2" _suffix="$3"
    local _file="${_name}-${_ver}${_suffix}"
    local _tar="$CACHE_DIR/$_file"
    local _url="${UPSTREAM_BASE}/${_file}"
    local _dir="$WORK_DIR/${_name}-${_ver}"
    if [ ! -f "$_tar" ]; then
        local _part="$_tar.part.$$"
        (
            if command -v flock >/dev/null 2>&1; then flock -x 9; fi
            if [ ! -f "$_tar" ]; then
                echo "==> downloading $_name $_ver"
                echo "    $_url"
                curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$_url"
                mv "$_part" "$_tar"
            fi
        ) 9>"$CACHE_DIR/.${_name}-download.lock"
        rm -f "$_part"
    fi
    # Always re-extract: we don't patch in-place, but tar is cheap and
    # an interrupted prior extraction would otherwise stay broken.
    rm -rf "$_dir"
    mkdir -p "$_dir"
    tar -C "$WORK_DIR" -xzf "$_tar"
}

fetch_and_extract buildsystem    "$BUILDSYSTEM_VERSION"  ".tar.gz"
fetch_and_extract libwapcaplet   "$WAPCAPLET_VERSION"    "-src.tar.gz"
fetch_and_extract libparserutils "$PARSERUTILS_VERSION"  "-src.tar.gz"
fetch_and_extract libcss         "$LIBCSS_VERSION"       "-src.tar.gz"

# Local patches — upstream fixes not yet in the pinned release. Applied to
# the freshly-extracted libcss source; the `-pN` suffix in `version`
# tracks the patch level so patched tarballs never collide with pristine
# ones. Currently: the select-engine free() of the static empty_bloom
# (upstream f1c3e3d1, fixed after 0.9.2) — on a root-element select any
# mid-selection error freed a .bss address, poisoning the heap freelist
# and corrupting the interned-string pool minutes later (apnews.com
# crashes).
for _patch in "$SCRIPT_DIR"/patches/*.patch; do
    [ -e "$_patch" ] || continue
    echo "==> applying $(basename "$_patch")"
    patch -d "$WORK_DIR/libcss-${LIBCSS_VERSION}" -p1 < "$_patch"
done

BUILDSYSTEM_DIR="$WORK_DIR/buildsystem-${BUILDSYSTEM_VERSION}"
[ -f "$BUILDSYSTEM_DIR/makefiles/Makefile.top" ] || {
    echo "buildsystem: missing makefiles/Makefile.top in $BUILDSYSTEM_DIR" >&2
    exit 1
}

rm -rf "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

#-----------------------------------------------------------------------------
# build_lib <subdir>
# Drives `make install` in a single NetSurf lib subtree. Pins:
#   NSSHARED        — buildsystem dir (where Makefile.top lives)
#   COMPONENT_TYPE  — lib-static so only the .a is produced (no .so)
#   PREFIX          — shared $INSTALL_DIR so subsequent libs find this
#                     one via PKG_CONFIG_PATH
# CC/AR/HOST passed when cross-compiling; CFLAGS/LDFLAGS carry the
# arch/macOS extras (env vars rather than `make CFLAGS=…` so the upstream
# `CFLAGS += -Werror=…` rules don't get clobbered).
#-----------------------------------------------------------------------------
build_lib() {
    local _dir="$1"
    local _args=(-C "$_dir" "-j${NCPU}" install
                 "COMPONENT_TYPE=lib-static"
                 "PREFIX=$INSTALL_DIR"
                 "NSSHARED=$BUILDSYSTEM_DIR"
                 "Q=")
    [ -n "$NS_HOST" ] && _args+=("HOST=$NS_HOST")
    [ -n "$NS_CC"   ] && _args+=("CC=$NS_CC")
    [ -n "$NS_AR"   ] && _args+=("AR=$NS_AR")
    echo "==> building $(basename "$_dir")"
    env \
        PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH:-}" \
        CFLAGS="${NS_EXTRA_CFLAGS} -Wno-error" \
        LDFLAGS="${NS_EXTRA_LDFLAGS}" \
        make "${_args[@]}"
}

# Order matters: parserutils + wapcaplet are leaves; libcss links both.
build_lib "$WORK_DIR/libwapcaplet-${WAPCAPLET_VERSION}"
build_lib "$WORK_DIR/libparserutils-${PARSERUTILS_VERSION}"
build_lib "$WORK_DIR/libcss-${LIBCSS_VERSION}"

#-----------------------------------------------------------------------------
# Stage. Mirror the lexbor-style flat lib/+include/ layout so the
# consumer-side libcss.cmake can point IMPORTED_LOCATION at predictable
# paths.
#-----------------------------------------------------------------------------
mkdir -p "$STAGE/lib" "$STAGE/include"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        cp -a "$INSTALL_DIR/$_D/." "$STAGE/lib/"
    fi
done
cp -a "$INSTALL_DIR/include/." "$STAGE/include/"

# Drop pkg-config metadata — the consumer cmake uses IMPORTED targets
# with hard paths; .pc files would carry build-time absolute paths
# anyway.
rm -rf "$STAGE/lib/pkgconfig"

#-----------------------------------------------------------------------------
# Verify outputs
#-----------------------------------------------------------------------------
for L in libcss.a libparserutils.a libwapcaplet.a; do
    [ -f "$STAGE/lib/$L" ] || { echo "missing $L in stage" >&2; exit 1; }
done
[ -f "$STAGE/include/libcss/libcss.h" ]            || { echo "missing libcss/libcss.h" >&2; exit 1; }
[ -f "$STAGE/include/libwapcaplet/libwapcaplet.h" ] || { echo "missing libwapcaplet/libwapcaplet.h" >&2; exit 1; }
[ -d "$STAGE/include/parserutils" ]                || { echo "missing parserutils/ headers" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "libcss $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 25 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,25p'
