#!/bin/bash
# Builds NetSurf-all 3.11 (libcss/libdom/libhubbub/etc. + core browser
# objects) for $TARGET_PLATFORM via NetSurf's own Makefile with
# TARGET=monkey. Produces the prebuilt-tarball that
# build-tools/cmake/libs/netsurf.cmake consumes — same shape consumers
# (src/yetty/ynetsurf, tools/ynetsurf) have today.
#
# Linux-x86_64 only (NetSurf's monkey build doesn't cross-compile cleanly).
# A guarded skip in build.sh keeps android/webasm/macos out.
#
# Output tarball layout (consumed by build-tools/cmake/libs/netsurf.cmake):
#   inst-monkey/
#     lib/{libcss.a, libdom.a, libhubbub.a, libparserutils.a,
#          libwapcaplet.a, libnsutils.a, libnsbmp.a, libnsgif.a,
#          libnslog.a, libnspsl.a, libsvgtiny.a, libutf8proc.a,
#          libnetsurf_core.a}
#     include/{dom/, hubbub/, libcss/, libutf8proc/, libwapcaplet/,
#              nslog/, nsutils/, parserutils/,
#              libnsbmp.h, librosprite.h, nsgif.h, nspsl.h, svgtiny.h}
#   netsurf/
#     include/netsurf/*.h         (public netsurf core API)
#     desktop/*.h utils/*.h content/*.h ...   (private headers
#                                              consumers reach into)
#     resources/...               (CSS, FatMessages, icons, ca-bundle,
#                                  per-locale dirs)
#   Messages-en                   (pre-split from FatMessages)

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

case "$TARGET_PLATFORM" in
    linux-x86_64) ;;
    *) echo "netsurf: $TARGET_PLATFORM not supported (linux-x86_64 only)" >&2; exit 2 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "$SCRIPT_DIR/version is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-netsurf-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || echo 4)"

# Upstream tarball: source-full release at netsurf-browser.org.
# Top-level extracted dir is netsurf-all-${VERSION}/.
URL="https://download.netsurf-browser.org/netsurf/releases/source-full/netsurf-all-${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/netsurf-all-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/netsurf-all-${VERSION}"
INST_DIR="$SRC_DIR/inst-monkey"
NS_CORE="$SRC_DIR/netsurf"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/netsurf-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$TARBALL_CACHE" ]; then
    _part="$TARBALL_CACHE.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$TARBALL_CACHE" ]; then
            echo "==> downloading netsurf-all ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$URL"
            mv "$_part" "$TARBALL_CACHE"
        fi
    ) 9>"$CACHE_DIR/.netsurf-download.lock"
    rm -f "$_part"
fi

# Always re-extract — we patch in place and we want a clean tree per build.
rm -rf "$SRC_DIR" "$STAGE"
echo "==> extracting -> $SRC_DIR"
tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"
mkdir -p "$STAGE"

#-----------------------------------------------------------------------------
# Patch: drop `Accept-Encoding: gzip` from netsurf's curl handler. yetty's
# prebuilt libcurl is built with -DCURL_ZLIB=OFF (and no brotli/zstd), so
# any gzip response trips CURLE_BAD_CONTENT_ENCODING. Idempotent.
#-----------------------------------------------------------------------------
_NS_CURL_C="$NS_CORE/content/fetchers/curl.c"
if [ -f "$_NS_CURL_C" ]; then
    if ! grep -q "yetty: gzip disabled" "$_NS_CURL_C"; then
        sed -i \
            "s|SETOPT(CURLOPT_ENCODING, \"gzip\");|SETOPT(CURLOPT_ENCODING, \"\"); /* yetty: gzip disabled — prebuilt libcurl has no zlib */|" \
            "$_NS_CURL_C"
        echo "==> patched $_NS_CURL_C (drop gzip Accept-Encoding)"
    fi
fi

#-----------------------------------------------------------------------------
# Build
#-----------------------------------------------------------------------------
# CFLAGS — newer gcc tightens warnings that NetSurf 3.11 trips. Match
# the existing yetty in-tree CMake build's flag list verbatim so the
# resulting object set is identical.
NS_CFLAGS="-Wno-error=redundant-decls -Wno-error=array-bounds \
-Wno-error=stringop-overflow -Wno-error=stringop-overread \
-Wno-error=stringop-truncation -Wno-error=use-after-free \
-Wno-error=dangling-pointer -Wno-error=address \
-Wno-error=cast-function-type -Wno-error=unused-result -Wno-error"

echo "==> building netsurf-all ${VERSION} (TARGET=monkey, -j${NCPU})"

# Inside a nix dev shell, pkg-config is a wrapper that hard-codes which
# env var it consumes at shell-init time, baked through utils.bash's
# `mangleVarListGeneric`. With `NIX_PKG_CONFIG_WRAPPER_TARGET_TARGET_*`
# set (the role for 3rdparty-netsurf), it reads `PKG_CONFIG_PATH_FOR_TARGET`
# — never plain `PKG_CONFIG_PATH`. We have to:
#   1) prepend our inst-monkey path to PKG_CONFIG_PATH_FOR_TARGET, AND
#   2) unset the `_FLAGS_SET_*` flag so add-flags.sh re-mangles on the
#      next pkg-config invocation (otherwise the wrapper uses the
#      pre-mangled value frozen at shell init).
# Both are no-ops outside nix shells.
_unset_args=()
while IFS= read -r _v; do
    _unset_args+=(-u "$_v")
done < <(env | sed -n 's/^\(NIX_PKG_CONFIG_WRAPPER_FLAGS_SET_[^=]*\)=.*$/\1/p')

env \
    "${_unset_args[@]}" \
    TARGET=monkey \
    PKG_CONFIG_PATH="$INST_DIR/lib/pkgconfig:${PKG_CONFIG_PATH:-}" \
    PKG_CONFIG_PATH_FOR_TARGET="$INST_DIR/lib/pkgconfig:${PKG_CONFIG_PATH_FOR_TARGET:-}" \
    CFLAGS="$NS_CFLAGS" \
    make -C "$SRC_DIR" -j"$NCPU" TARGET=monkey

#-----------------------------------------------------------------------------
# Locate the per-host build dir (netsurf/build/<friendly>-monkey).
# On Linux it's typically Linux-monkey but we glob to be host-agnostic.
#-----------------------------------------------------------------------------
BUILD_PARENT="$NS_CORE/build"
NS_BUILDDIR=""
for d in "$BUILD_PARENT"/*-monkey*; do
    if [ -d "$d" ]; then NS_BUILDDIR="$d"; break; fi
done
[ -n "$NS_BUILDDIR" ] || { echo "no *-monkey* build dir under $BUILD_PARENT" >&2; exit 1; }
echo "==> netsurf core build dir: $NS_BUILDDIR"

#-----------------------------------------------------------------------------
# ar the netsurf core .o files (everything EXCEPT frontends/*) into a
# single libnetsurf_core.a. yetty plugs in its own ynetsurf frontend
# tables in place of monkey's at link time.
#
# NetSurf's build emits objects with two naming schemes — match both:
#   build_Linux-monkey_frontends_monkey_main.o    (flattened)
#   frontends/monkey/main.o                       (path)
#-----------------------------------------------------------------------------
NS_CORE_LIB="$INST_DIR/lib/libnetsurf_core.a"
ARGFILE="$NS_BUILDDIR/.netsurf-core.objs"
: > "$ARGFILE"
( cd "$NS_BUILDDIR" && find . -name '*.o' -type f -print ) | \
    sed 's|^\./||' | \
    grep -Ev '^(frontends|build_[^_]*_frontends_)' | \
    while read -r o; do echo "$NS_BUILDDIR/$o" >> "$ARGFILE"; done

if [ ! -s "$ARGFILE" ]; then
    echo "no .o files matched for libnetsurf_core.a (empty argfile)" >&2
    exit 1
fi
NCOUNT="$(wc -l < "$ARGFILE")"
echo "==> ar libnetsurf_core.a ($NCOUNT objects)"
rm -f "$NS_CORE_LIB"
ar rcs "$NS_CORE_LIB" "@$ARGFILE"

#-----------------------------------------------------------------------------
# Pre-generate Messages-en from FatMessages using the host-built
# split-messages tool (a side-effect of the monkey build under
# netsurf/build/<friendly>-monkey/tools/split-messages). yetty's
# consumer expects this file at runtime to translate keys like
# `messages_get("FetchErrorTitle")`.
#-----------------------------------------------------------------------------
SPLIT_MSG="$NS_BUILDDIR/tools/split-messages"
[ -x "$SPLIT_MSG" ] || { echo "split-messages not built: $SPLIT_MSG" >&2; exit 1; }
MSGOUT="$STAGE/Messages-en"
"$SPLIT_MSG" -l en -p any -f messages -o "$MSGOUT" "$NS_CORE/resources/FatMessages"
[ -s "$MSGOUT" ] || { echo "Messages-en is empty" >&2; exit 1; }

#-----------------------------------------------------------------------------
# Stage. Mirror the existing in-tree layout exactly so the new
# netsurf.cmake can point YETTY_NETSURF_ROOT at the extracted tarball
# and consumers see the same paths they always have:
#   ${YETTY_NETSURF_ROOT}/inst-monkey/{lib,include}
#   ${YETTY_NETSURF_ROOT}/netsurf/{include,desktop,utils,content,resources}
#-----------------------------------------------------------------------------
mkdir -p "$STAGE/inst-monkey/lib" "$STAGE/inst-monkey/include"
mkdir -p "$STAGE/netsurf/include" "$STAGE/netsurf/desktop" \
         "$STAGE/netsurf/utils"   "$STAGE/netsurf/content" \
         "$STAGE/netsurf/resources"

# Helper-lib archives + core archive.
cp -a "$INST_DIR/lib/." "$STAGE/inst-monkey/lib/"
# Drop pkgconfig (consumer cmake doesn't read it; saves a few KB and
# avoids stale absolute paths from the build host leaking into the tarball).
rm -rf "$STAGE/inst-monkey/lib/pkgconfig"

# Helper-lib public headers.
cp -a "$INST_DIR/include/." "$STAGE/inst-monkey/include/"

# NetSurf core public + private headers and resources. Take only the
# subdirs the in-tree build exposes — skip frontends/, test/, docs/,
# tools/, build/, .github/, the binary nsmonkey, etc.
cp -a "$NS_CORE/include/."   "$STAGE/netsurf/include/"
cp -a "$NS_CORE/desktop/."   "$STAGE/netsurf/desktop/"
cp -a "$NS_CORE/utils/."     "$STAGE/netsurf/utils/"
cp -a "$NS_CORE/content/."   "$STAGE/netsurf/content/"
cp -a "$NS_CORE/resources/." "$STAGE/netsurf/resources/"

# Strip *.c sources from desktop/utils/content — we only need headers
# from those trees (the .o files are already in libnetsurf_core.a).
find "$STAGE/netsurf/desktop" "$STAGE/netsurf/utils" "$STAGE/netsurf/content" \
    -type f -name '*.c' -delete

#-----------------------------------------------------------------------------
# Verify expected outputs
#-----------------------------------------------------------------------------
_REQUIRED_LIBS=(
    libcss.a libdom.a libhubbub.a libparserutils.a libwapcaplet.a
    libnsutils.a libnsbmp.a libnsgif.a libnslog.a libnspsl.a
    libsvgtiny.a libutf8proc.a
    libnetsurf_core.a
)
for L in "${_REQUIRED_LIBS[@]}"; do
    [ -f "$STAGE/inst-monkey/lib/$L" ] || {
        echo "missing $L in stage" >&2; exit 1; }
done
[ -f "$STAGE/netsurf/include/netsurf/netsurf.h" ] || \
    { echo "missing netsurf/netsurf.h" >&2; exit 1; }
[ -f "$STAGE/netsurf/desktop/gui_table.h" ] || \
    { echo "missing desktop/gui_table.h" >&2; exit 1; }
[ -f "$STAGE/netsurf/utils/log.h" ] || \
    { echo "missing utils/log.h" >&2; exit 1; }
[ -f "$STAGE/netsurf/content/fetch.h" ] || \
    { echo "missing content/fetch.h" >&2; exit 1; }
[ -f "$STAGE/netsurf/resources/default.css" ] || \
    { echo "missing resources/default.css" >&2; exit 1; }
[ -s "$STAGE/Messages-en" ] || \
    { echo "missing Messages-en" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "netsurf-all $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents: $ENTRIES files; first 25:"
tar -tzf "$TARBALL" | sed -n '1,25p'
