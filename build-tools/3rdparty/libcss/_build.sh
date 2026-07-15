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
# Build approach: the three libs + the NetSurf buildsystem are VENDORED
# in-repo under vendor/ (no upstream fetch — the source is edited
# directly, see vendor/README). Copy each into the work tree and drive
# `make install` with COMPONENT_TYPE=lib-static so only the static .a is
# produced. Same CC/HOST/AR cross-compile pattern netsurf/_build.sh uses;
# deps are built in order (wapcaplet → parserutils → libcss), each
# pointing at the previous via PKG_CONFIG_PATH inside a single INSTALL
# prefix.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "$SCRIPT_DIR/version is empty" >&2; exit 1; }

# The three libs + the NetSurf buildsystem are VENDORED under
# vendor/{buildsystem,libwapcaplet,libparserutils,libcss}. There is no
# upstream fetch — the source is in-repo and edited directly (see
# vendor/README). `version` is only the package/tarball/release version.
WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-libcss-$TARGET_PLATFORM}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libcss-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR"

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
stage_vendored() {
    local _name="$1"
    local _src="$SCRIPT_DIR/vendor/$_name"
    local _dir="$WORK_DIR/$_name"
    [ -d "$_src" ] || { echo "vendor: missing source tree $_src" >&2; exit 1; }
    # Copy the in-repo vendored source into the work tree — the NetSurf
    # make build writes build/ dirs into the source, so we never build in
    # place (keeps the checked-in vendor/ tree clean).
    rm -rf "$_dir"
    cp -a "$_src" "$_dir"
}

stage_vendored buildsystem
stage_vendored libwapcaplet
stage_vendored libparserutils
stage_vendored libcss

BUILDSYSTEM_DIR="$WORK_DIR/buildsystem"
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
build_lib "$WORK_DIR/libwapcaplet"
build_lib "$WORK_DIR/libparserutils"
build_lib "$WORK_DIR/libcss"

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
