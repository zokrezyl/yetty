#!/bin/bash
# Builds NetSurf-all 3.11 (libcss/libdom/libhubbub/etc. + core browser
# objects) for $TARGET_PLATFORM via NetSurf's own Makefile with
# TARGET=monkey. Produces the prebuilt-tarball that
# build-tools/cmake/libs/netsurf.cmake consumes.
#
# Output tarball layout:
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
#   Messages-en                   (pre-split from FatMessages — only
#                                  produced on native builds where
#                                  the host-built split-messages tool
#                                  runs; cross builds ship FatMessages
#                                  and let the consumer's cmake split
#                                  using a host split-messages binary)
#
# Cross-compile philosophy: NetSurf's Makefile is Unix-centric and only
# truly cross-compiles via CC/HOST overrides. For each platform we
# either set those (when a usable cross toolchain is on PATH) or bail
# with a clear "not supported" message. Per-platform matrix here
# mirrors lexbor's so the CI surface is uniform; some entries are
# genuine no-ops (windows-MSVC / webasm / iOS-tvOS) where NetSurf's
# build system cannot reach.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "$SCRIPT_DIR/version is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-netsurf-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

URL="https://download.netsurf-browser.org/netsurf/releases/source-full/netsurf-all-${VERSION}.tar.gz"
TARBALL_CACHE="$CACHE_DIR/netsurf-all-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/netsurf-all-${VERSION}"
INST_DIR="$SRC_DIR/inst-monkey"
NS_CORE="$SRC_DIR/netsurf"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/netsurf-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Per-platform setup. NS_HOST/NS_CC/NS_AR are passed to NetSurf's Makefile
# as `make HOST=… CC=… AR=…`. NS_NATIVE=1 means the build host can run
# the produced binaries (relevant for split-messages pre-generation).
#-----------------------------------------------------------------------------
NS_HOST=""
NS_BUILD=""
NS_CC=""
NS_CXX=""
NS_AR=""
NS_RANLIB=""
NS_NATIVE=0
NS_EXTRA_CFLAGS=""
NS_EXTRA_LDFLAGS=""
# Disable Duktape (NetSurf's JS engine) for cross builds — its build
# requires nsgenbind to RUN on the build host, but our top-level
# CC=cross-gcc override propagates into nsgenbind's sub-make and
# produces a target-arch nsgenbind binary that can't exec on the build
# machine. yetty doesn't use NetSurf's JS path anyway (its own quickjs
# binding lives in src/yetty/ylexbor — independent of NetSurf core).
NS_USE_DUKTAPE="YES"

case "$TARGET_PLATFORM" in

linux-x86_64)
    NS_NATIVE=1
    ;;

macos-x86_64)
    NS_NATIVE=1
    # _DARWIN_C_SOURCE bumps __DARWIN_C_LEVEL to __DARWIN_C_FULL which
    # exposes BSD-only declarations in <arpa/inet.h> (inet_aton in
    # particular — netsurf's content/urldb.c calls it). Without it,
    # clang's C99-strict default rejects the call as implicit.
    # Also: nsgenbind-generated WebIDL bindings (canvas_rendering_*)
    # call strcasecmp without including <strings.h>; downgrade
    # implicit-function-declaration from error to warning so those
    # generated files compile.
    NS_EXTRA_CFLAGS="-arch x86_64 -D_DARWIN_C_SOURCE -Wno-error=implicit-function-declaration"
    NS_EXTRA_LDFLAGS="-arch x86_64"
    ;;

macos-arm64)
    NS_NATIVE=1
    NS_EXTRA_CFLAGS="-arch arm64 -D_DARWIN_C_SOURCE -Wno-error=implicit-function-declaration"
    NS_EXTRA_LDFLAGS="-arch arm64"
    ;;

linux-aarch64|linux-riscv64|\
android-arm64-v8a|android-x86_64|\
ios-arm64|ios-x86_64|tvos-x86_64|tvos-arm64|\
webasm|windows-x86_64)
    # NetSurf monkey TARGET assumes a POSIX environment (forks, dlfcn,
    # POSIX file APIs) and a Unix-style toolchain. The remaining
    # tarball-target platforms either:
    #   - Android: would need NDK-cross builds of expat/libxml2/jpeg/
    #     png/webp + a netsurf-aware NDK toolchain pkgconf path. Real
    #     cross-compile is feasible but a separate workstream — for now
    #     the matrix entry is here for shape parity with lexbor.
    #   - iOS / tvOS: upstream frontend lives in an Xcode project, not
    #     the monkey Makefile we drive here.
    #   - WebAssembly: POSIX socket / dlfcn assumptions in netsurf core.
    #   - Windows MSVC: Unix-style Makefile, mingw-only upstream.
    #
    # Ship a placeholder tarball carrying only an UNSUPPORTED marker
    # so the matrix entry doesn't poison the GitHub release step.
    # consumer-side netsurf.cmake detects the marker and skips silently.
    echo "netsurf: $TARGET_PLATFORM is not built (see UNSUPPORTED in tarball)" >&2
    rm -rf "$STAGE"
    mkdir -p "$STAGE"
    cat > "$STAGE/UNSUPPORTED" <<EOF
netsurf-all $VERSION is not built for $TARGET_PLATFORM.

NetSurf's TARGET=monkey build system assumes a POSIX environment with
a Unix-style toolchain. The platforms left out of the real build are:
  - linux-aarch64, linux-riscv64
        nix's cross glibc 2.40 ships clang-only fortify macros
        (__fortify_clang_overload_arg in bits/inet-fortified.h) that
        gcc 15 cross-compilers can't parse. Outside the scope of what
        the build script can paper over — needs upstream nixpkgs fix
        or a switch to clang-cross.
  - android-arm64-v8a, android-x86_64
        Cross-compile via NDK is feasible but requires NDK-built
        expat/libxml2/jpeg/png/webp — separate workstream.
  - ios-arm64, ios-x86_64, tvos-arm64, tvos-x86_64
        Upstream frontend is an Xcode project, not the monkey Makefile.
  - webasm
        POSIX socket / dlfcn assumptions in netsurf core.
  - windows-x86_64
        Unix-style Makefile; upstream supports mingw, not native MSVC.

The yetty consumer side (build-tools/cmake/libs/netsurf.cmake) detects
this UNSUPPORTED marker and skips netsurf_core silently on these
platforms — the rest of yetty configures unaffected.
EOF
    tar -C "$STAGE" -czf "$TARBALL" .
    echo "netsurf $VERSION ($TARGET_PLATFORM) — placeholder tarball written:"
    ls -lh "$TARBALL"
    exit 0
    ;;

*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

#-----------------------------------------------------------------------------
# Transitive prebuilt dep: openssl-new — same tarball libcurl is built
# against. NetSurf's content/fetchers/about/certificate.c and
# content/fetchers/curl.c reference OpenSSL APIs (BIO_*, ASN1_*,
# EVP_PKEY_*, X509_*) and pkg-config on this build host has to resolve
# them. Only needed for real-build platforms — placeholder branches
# exit above. Linux/macOS ship libssl.a + libcrypto.a; we don't fetch
# openssl-new for the placeholder platforms (Windows uses .lib, but
# we never reach this for windows-x86_64 anyway).
#-----------------------------------------------------------------------------
OPENSSL_VERSION_FILE="$SCRIPT_DIR/../openssl-new/version"
[ -f "$OPENSSL_VERSION_FILE" ] || { echo "missing $OPENSSL_VERSION_FILE" >&2; exit 1; }
OPENSSL_NEW_VERSION="$(tr -d '[:space:]' < "$OPENSSL_VERSION_FILE")"
GH_RELEASE_BASE="${GH_RELEASE_BASE:-https://github.com/zokrezyl/yetty/releases/download}"
OSSL_FILENAME="openssl-new-${TARGET_PLATFORM}-${OPENSSL_NEW_VERSION}.tar.gz"
OSSL_TARBALL="$CACHE_DIR/$OSSL_FILENAME"
OSSL_URL="${GH_RELEASE_BASE}/lib-openssl-new-${OPENSSL_NEW_VERSION}/${OSSL_FILENAME}"
OSSL_DIR="$WORK_DIR/openssl-new-${OPENSSL_NEW_VERSION}"

if [ ! -f "$OSSL_TARBALL" ]; then
    _part="$OSSL_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$OSSL_TARBALL" ]; then
            echo "==> downloading openssl-new ${OPENSSL_NEW_VERSION} for ${TARGET_PLATFORM}"
            echo "    $OSSL_URL"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors -o "$_part" "$OSSL_URL"
            mv "$_part" "$OSSL_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.openssl-new-download.lock"
    rm -f "$_part"
fi
if [ ! -d "$OSSL_DIR" ]; then
    echo "==> extracting openssl-new -> $OSSL_DIR"
    mkdir -p "$OSSL_DIR"
    tar -C "$OSSL_DIR" -xzf "$OSSL_TARBALL"
fi
[ -f "$OSSL_DIR/lib/libssl.a"           ] || { echo "openssl-new: missing libssl.a"   >&2; exit 1; }
[ -f "$OSSL_DIR/lib/libcrypto.a"        ] || { echo "openssl-new: missing libcrypto.a">&2; exit 1; }
[ -f "$OSSL_DIR/include/openssl/ssl.h"  ] || { echo "openssl-new: missing ssl.h"      >&2; exit 1; }

# Rewrite the .pc files in place so `prefix=...` points at the actual
# extraction dir, not the openssl-new build-time path encoded by the
# openssl-new release runner.
for _PC in "$OSSL_DIR"/lib/pkgconfig/*.pc; do
    [ -f "$_PC" ] || continue
    sed -i.bak -E "s|^prefix=.*$|prefix=${OSSL_DIR}|" "$_PC"
    rm -f "$_PC.bak"
done

#-----------------------------------------------------------------------------
# Fetch + extract NetSurf source
#-----------------------------------------------------------------------------
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
NS_CFLAGS="-Wno-error=redundant-decls -Wno-error=array-bounds \
-Wno-error=stringop-overflow -Wno-error=stringop-overread \
-Wno-error=stringop-truncation -Wno-error=use-after-free \
-Wno-error=dangling-pointer -Wno-error=address \
-Wno-error=cast-function-type -Wno-error=unused-result -Wno-error \
${NS_EXTRA_CFLAGS}"

# Inside a nix dev shell, pkg-config is a wrapper that hard-codes which
# env var it consumes at shell-init time, baked through utils.bash's
# `mangleVarListGeneric`. With `NIX_PKG_CONFIG_WRAPPER_TARGET_TARGET_*`
# set, it reads `PKG_CONFIG_PATH_FOR_TARGET` — never plain
# `PKG_CONFIG_PATH`. So we both prepend our inst-monkey path to
# PKG_CONFIG_PATH_FOR_TARGET AND unset the `_FLAGS_SET_*` flag so the
# wrapper re-mangles on the next pkg-config invocation. Both are
# no-ops outside nix shells.
_unset_args=()
while IFS= read -r _v; do
    _unset_args+=(-u "$_v")
done < <(env | sed -n 's/^\(NIX_PKG_CONFIG_WRAPPER_FLAGS_SET_[^=]*\)=.*$/\1/p')

# Compose make command line. NetSurf's Makefile honours the standard
# CC/HOST/AR/RANLIB overrides for cross-compile, plus BUILD for the
# build-host triple (used by host-side tools like nsgenbind).
_make_args=(-C "$SRC_DIR" "-j${NCPU}" TARGET=monkey \
    "NETSURF_USE_DUKTAPE=$NS_USE_DUKTAPE")
[ -n "$NS_BUILD"  ] && _make_args+=("BUILD=$NS_BUILD")
[ -n "$NS_HOST"   ] && _make_args+=("HOST=$NS_HOST")
[ -n "$NS_CC"     ] && _make_args+=("CC=$NS_CC")
[ -n "$NS_CXX"    ] && _make_args+=("CXX=$NS_CXX")
[ -n "$NS_AR"     ] && _make_args+=("AR=$NS_AR")
[ -n "$NS_RANLIB" ] && _make_args+=("RANLIB=$NS_RANLIB")
[ -n "$NS_EXTRA_LDFLAGS" ] && _make_args+=("LDFLAGS=$NS_EXTRA_LDFLAGS")

echo "==> building netsurf-all ${VERSION} (TARGET=monkey, host=${NS_HOST:-native}, -j${NCPU})"
env \
    "${_unset_args[@]}" \
    PKG_CONFIG_PATH="$INST_DIR/lib/pkgconfig:$OSSL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH:-}" \
    PKG_CONFIG_PATH_FOR_TARGET="$INST_DIR/lib/pkgconfig:$OSSL_DIR/lib/pkgconfig:${PKG_CONFIG_PATH_FOR_TARGET:-}" \
    CFLAGS="$NS_CFLAGS" \
    make "${_make_args[@]}"

#-----------------------------------------------------------------------------
# Locate the per-host build dir (netsurf/build/<friendly>-monkey).
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
"${NS_AR:-ar}" rcs "$NS_CORE_LIB" "@$ARGFILE"

#-----------------------------------------------------------------------------
# Pre-generate Messages-en from FatMessages — only feasible on native
# builds where split-messages was just compiled with the host CC. For
# cross builds, we ship FatMessages and let the consumer split it.
#-----------------------------------------------------------------------------
SPLIT_MSG="$NS_BUILDDIR/tools/split-messages"
MSGOUT_PATH=""
if [ "$NS_NATIVE" = "1" ] && [ -x "$SPLIT_MSG" ]; then
    MSGOUT_PATH="$STAGE/Messages-en"
    "$SPLIT_MSG" -l en -p any -f messages -o "$MSGOUT_PATH" \
        "$NS_CORE/resources/FatMessages"
    [ -s "$MSGOUT_PATH" ] || \
        { echo "Messages-en is empty after split" >&2; exit 1; }
    echo "==> generated Messages-en ($(wc -c <"$MSGOUT_PATH") bytes)"
else
    echo "==> skipping Messages-en pre-gen (cross build); shipping FatMessages"
fi

#-----------------------------------------------------------------------------
# Stage. Mirror in-tree layout so consumer cmake can point
# YETTY_NETSURF_ROOT at the extracted tarball.
#-----------------------------------------------------------------------------
mkdir -p "$STAGE/inst-monkey/lib" "$STAGE/inst-monkey/include"
mkdir -p "$STAGE/netsurf/include" "$STAGE/netsurf/desktop" \
         "$STAGE/netsurf/utils"   "$STAGE/netsurf/content" \
         "$STAGE/netsurf/resources"

cp -a "$INST_DIR/lib/." "$STAGE/inst-monkey/lib/"
rm -rf "$STAGE/inst-monkey/lib/pkgconfig"
cp -a "$INST_DIR/include/." "$STAGE/inst-monkey/include/"

cp -a "$NS_CORE/include/."   "$STAGE/netsurf/include/"
cp -a "$NS_CORE/desktop/."   "$STAGE/netsurf/desktop/"
cp -a "$NS_CORE/utils/."     "$STAGE/netsurf/utils/"
cp -a "$NS_CORE/content/."   "$STAGE/netsurf/content/"
cp -a "$NS_CORE/resources/." "$STAGE/netsurf/resources/"

# Strip *.c sources from desktop/utils/content — only headers needed.
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
if [ "$NS_NATIVE" = "1" ]; then
    [ -s "$STAGE/Messages-en" ] || \
        { echo "missing Messages-en (native build)" >&2; exit 1; }
fi

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "netsurf-all $VERSION ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents: $ENTRIES files; first 25:"
tar -tzf "$TARBALL" | sed -n '1,25p'
