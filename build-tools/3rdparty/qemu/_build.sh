#!/bin/bash
# Builds qemu-system-riscv64 for $TARGET_PLATFORM and packages it as a
# tarball under $OUTPUT_DIR. All per-platform logic lives inline below
# in a case block — no separate platform scripts (except windows, which
# is big enough to justify its own file at platforms/windows-x86_64.sh).
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 |
#                     android-arm64-v8a | android-x86_64 |
#                     macos-arm64 | macos-x86_64 |
#                     ios-arm64 | ios-x86_64 |
#                     tvos-arm64 | tvos-x86_64 |
#                     windows-x86_64
#   VERSION           derived — read from ./version, used in tarball name
#   OUTPUT_DIR        where the tarball is written
# Optional env:
#   WORK_DIR          default /tmp/yetty-asset-qemu-$TARGET_PLATFORM
#   CACHE_DIR         default $HOME/.cache/yetty-qemu-assets
#                     holds the QEMU source tarball so multi-target builds
#                     share a single download
#
# The `version` file format is <upstream>-<pkg-rev>, e.g. `11.0.0-rc4-1` —
# single source of truth for both the upstream QEMU tarball fetched here
# AND the lib-qemu-<version> release tag / qemu-<platform>-<version>.tar.gz
# tarball name. Bump <pkg-rev> for packaging-only changes (configure-flag
# tweaks, applied patches); bump <upstream> when moving to a new QEMU
# release.
#
# QEMU configure flags are kept in sync with build-tools/cmake/qemu.cmake.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
VERSION_FILE="$(dirname "$0")/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

# Split <upstream>-<pkg-rev>. QEMU upstream tags routinely contain `-`
# (e.g. 11.0.0-rc4), so split off the last `-` component as the pkg rev.
QEMU_VERSION="${VERSION%-*}"
PKG_REV="${VERSION##*-}"
[ "$QEMU_VERSION" != "$VERSION" ] && [ -n "$PKG_REV" ] || {
    echo "$VERSION_FILE: expected <upstream>-<rev>, got '$VERSION'" >&2
    exit 1
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
WORK_DIR="${WORK_DIR:-/tmp/yetty-asset-qemu-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-qemu-assets}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

QEMU_URL="https://download.qemu.org/qemu-${QEMU_VERSION}.tar.xz"
QEMU_TARBALL="$CACHE_DIR/qemu-${QEMU_VERSION}.tar.xz"
SRC_DIR="$WORK_DIR/qemu-${QEMU_VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/qemu-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"
cd "$WORK_DIR"

#-----------------------------------------------------------------------------
# Fetch (shared across targets) + extract QEMU source
#-----------------------------------------------------------------------------
if [ ! -f "$QEMU_TARBALL" ]; then
    # Serialize with a flock so parallel per-target builds share one
    # download. Per-PID .part file avoids clobbering if the lock is
    # unavailable (e.g. no flock on this host — fall through harmlessly).
    _part="$QEMU_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then
            flock -x 9
        fi
        if [ ! -f "$QEMU_TARBALL" ]; then
            echo "==> downloading QEMU ${QEMU_VERSION} to cache ($QEMU_TARBALL)"
            curl -fL --retry 3 -o "$_part" "$QEMU_URL"
            mv "$_part" "$QEMU_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.qemu-download.lock"
    rm -f "$_part"
else
    echo "==> using cached QEMU tarball: $QEMU_TARBALL"
fi
if [ ! -f "$SRC_DIR/configure" ]; then
    echo "==> extracting QEMU"
    # Skip roms/ (firmware source trees: u-boot, edk2, skiboot — heavy
    # symlink churn that breaks extraction on Windows without Developer
    # Mode). riscv64-softmmu doesn't need them — it uses pre-built blobs
    # from pc-bios/.
    # Skip tests/lcitool/libvirt-ci too (nested submodule with prep-script
    # symlinks). Keep tests/lcitool/Makefile.include — QEMU's top-level
    # Makefile unconditionally `include`s it.
    tar xf "$QEMU_TARBALL" \
        --exclude='qemu-*/roms' \
        --exclude='qemu-*/tests/lcitool/libvirt-ci'
fi

# Pruned device config (shared with poc/qemu)
DEVCFG_DIR="$SRC_DIR/configs/devices/riscv64-softmmu"
mkdir -p "$DEVCFG_DIR"
cp "$REPO_ROOT/poc/qemu/configs/riscv64-softmmu/default.mak" "$DEVCFG_DIR/default.mak"

#-----------------------------------------------------------------------------
# Common configure flags — mirror build-tools/cmake/qemu.cmake.
#-----------------------------------------------------------------------------
_CONFIGURE_ARGS=(
    --target-list=riscv64-softmmu
    --without-default-features
    --enable-tcg
    --enable-slirp
    --enable-fdt=internal
    --enable-trace-backends=nop
    --disable-werror
    --disable-docs
    --disable-guest-agent
    --disable-tools
    --disable-qom-cast-debug
    --disable-coroutine-pool
    # No display surface — qemu's ui/console.c stubs out pixman_image_t when
    # this is set. Without it, --without-default-features still leaves pixman
    # as a hard link-time requirement for any softmmu target.
    --disable-pixman
)
# virtfs is platform-specific: needs POSIX 9p machinery + xattr that doesn't
# exist on Windows. Each platform block opts in below.
_EXTRA_CFLAGS="-Os -ffunction-sections -fdata-sections"
_EXTRA_CXXFLAGS="-Os -ffunction-sections -fdata-sections"
_EXTRA_LDFLAGS="-Wl,--gc-sections"
_QEMU_BINARY_NAME="qemu-system-riscv64"
_QEMU_OUTPUT_NAME=""     # defaults to _QEMU_BINARY_NAME at packaging time
_STRIP_BIN="strip"

#-----------------------------------------------------------------------------
# Per-platform block: sets toolchain/SDK flags and, where needed, builds a
# dependency sysroot (android) or patches QEMU's configure (windows).
#-----------------------------------------------------------------------------
case "$TARGET_PLATFORM" in

linux-x86_64)
    _CONFIGURE_ARGS+=(--enable-virtfs --enable-attr --cc=gcc --cxx=g++)
    ;;

linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-unknown-linux-gnu-}"
    _CONFIGURE_ARGS+=(
        --enable-virtfs
        --enable-attr
        --cross-prefix="$CROSS_PREFIX"
        --cc="${CROSS_PREFIX}gcc"
        --cxx="${CROSS_PREFIX}g++"
        --host-cc=gcc
    )
    _STRIP_BIN="${CROSS_PREFIX}strip"
    ;;

android-arm64-v8a|android-x86_64)
    # NDK-direct cross build. The .#assets-qemu-android-* nix shell must
    # put the NDK triple-prefixed clang on PATH and export ANDROID_NDK_HOME.
    : "${ANDROID_API:=28}"   # bionic gained iconv at API 28, glib needs it
    : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set — source the .#assets-qemu-android-* shell}"

    case "$TARGET_PLATFORM" in
        android-arm64-v8a) ANDROID_TRIPLE="aarch64-linux-android"; ANDROID_CPU="aarch64" ;;
        android-x86_64)    ANDROID_TRIPLE="x86_64-linux-android";  ANDROID_CPU="x86_64"  ;;
    esac

    _CC="${ANDROID_TRIPLE}${ANDROID_API}-clang"
    _CXX="${ANDROID_TRIPLE}${ANDROID_API}-clang++"
    command -v "$_CC" >/dev/null || {
        echo "error: $_CC not on PATH (expected via NDK shellHook)" >&2
        exit 1
    }

    # Build pcre2/libffi/glib into a per-ABI sysroot. nixpkgs
    # pkgsCross.*-android hits a compiler-rt/pthread.h regression on
    # clang 19+, so avoid it entirely.
    PCRE2_VERSION="10.44"
    LIBFFI_VERSION="3.4.6"
    GLIB_VERSION="2.80.5"
    SYSROOT="$WORK_DIR/android-sysroot-${TARGET_PLATFORM##android-}"
    SYSROOT_STAMP="$SYSROOT/.built-$PCRE2_VERSION-$LIBFFI_VERSION-$GLIB_VERSION"
    DEPS_DIR="$WORK_DIR/android-deps-src"
    mkdir -p "$SYSROOT" "$DEPS_DIR"

    CROSSFILE="$SYSROOT/android-${TARGET_PLATFORM##android-}.ini"
    cat > "$CROSSFILE" <<CROSS_EOF
[binaries]
c         = '$_CC'
cpp       = '$_CXX'
ar        = 'llvm-ar'
strip     = 'llvm-strip'
ranlib    = 'llvm-ranlib'
pkg-config = 'pkg-config'

[host_machine]
system     = 'android'
cpu_family = '$ANDROID_CPU'
cpu        = '$ANDROID_CPU'
endian     = 'little'
CROSS_EOF

    _fetch() {
        local url="$1" out="$2"
        if [ ! -f "$DEPS_DIR/$out" ]; then
            echo "  fetch $out"
            curl -fL --retry 3 -o "$DEPS_DIR/$out.part" "$url"
            mv "$DEPS_DIR/$out.part" "$DEPS_DIR/$out"
        fi
    }

    _autotools_build() {
        local name="$1" src="$2"
        echo "==> android sysroot: $name"
        (
            cd "$src"
            if [ ! -f "build-${TARGET_PLATFORM}/Makefile" ]; then
                rm -rf "build-${TARGET_PLATFORM}"
                mkdir -p "build-${TARGET_PLATFORM}"
                (
                    cd "build-${TARGET_PLATFORM}"
                    CC="$_CC" AR=llvm-ar RANLIB=llvm-ranlib \
                        ../configure --host="$ANDROID_TRIPLE" \
                            --prefix="$SYSROOT" \
                            --disable-shared --enable-static
                )
            fi
            cd "build-${TARGET_PLATFORM}"
            make -j"$NCPU"
            make install
        )
    }

    _meson_build() {
        local name="$1" src="$2"; shift 2
        echo "==> android sysroot: $name"
        rm -rf "$DEPS_DIR/$name-build"
        # No --wrap-mode=nodownload: glib requires the proxy-libintl wrap
        # subproject on systems without libintl (Android bionic), and
        # meson needs to fetch it via the wrap DB.
        meson setup "$DEPS_DIR/$name-build" "$src" \
            --cross-file="$CROSSFILE" \
            --prefix="$SYSROOT" \
            --buildtype=release \
            --default-library=static \
            "$@"
        meson install -C "$DEPS_DIR/$name-build"
    }

    if [ -f "$SYSROOT_STAMP" ]; then
        echo "==> android sysroot already built: $SYSROOT"
    else
        echo "==> building android sysroot (pcre2, libffi, glib) for $TARGET_PLATFORM"

        # pcre2 — glib hard-dep. 10.44's tarball ships autotools only.
        _fetch "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${PCRE2_VERSION}/pcre2-${PCRE2_VERSION}.tar.bz2" \
            "pcre2-${PCRE2_VERSION}.tar.bz2"
        [ -d "$DEPS_DIR/pcre2-${PCRE2_VERSION}" ] || tar -C "$DEPS_DIR" -xjf "$DEPS_DIR/pcre2-${PCRE2_VERSION}.tar.bz2"
        _autotools_build "pcre2" "$DEPS_DIR/pcre2-${PCRE2_VERSION}"

        # libffi — glib hard-dep.
        _fetch "https://github.com/libffi/libffi/releases/download/v${LIBFFI_VERSION}/libffi-${LIBFFI_VERSION}.tar.gz" \
            "libffi-${LIBFFI_VERSION}.tar.gz"
        [ -d "$DEPS_DIR/libffi-${LIBFFI_VERSION}" ] || tar -C "$DEPS_DIR" -xzf "$DEPS_DIR/libffi-${LIBFFI_VERSION}.tar.gz"
        _autotools_build "libffi" "$DEPS_DIR/libffi-${LIBFFI_VERSION}"

        # glib — QEMU's main dep (meson).
        GLIB_MINOR="${GLIB_VERSION%.*}"
        _fetch "https://download.gnome.org/sources/glib/${GLIB_MINOR}/glib-${GLIB_VERSION}.tar.xz" \
            "glib-${GLIB_VERSION}.tar.xz"
        [ -d "$DEPS_DIR/glib-${GLIB_VERSION}" ] || tar -C "$DEPS_DIR" -xJf "$DEPS_DIR/glib-${GLIB_VERSION}.tar.xz"
        export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig:$SYSROOT/lib64/pkgconfig"
        # Empty, not $SYSROOT: the .pc files we just installed via
        # `--prefix=$SYSROOT` contain absolute paths already; a non-empty
        # PKG_CONFIG_SYSROOT_DIR would double-prefix them.
        export PKG_CONFIG_SYSROOT_DIR=""
        _meson_build "glib" "$DEPS_DIR/glib-${GLIB_VERSION}" \
            -Dtests=false \
            -Dinstalled_tests=false \
            -Dnls=disabled \
            -Dselinux=disabled \
            -Dxattr=false \
            -Dlibmount=disabled \
            -Dintrospection=disabled \
            -Ddocumentation=false \
            -Dman-pages=disabled \
            -Dsysprof=disabled \
            -Doss_fuzz=disabled \
            -Dglib_debug=disabled

        touch "$SYSROOT_STAMP"
        echo "==> android sysroot ready: $SYSROOT"
    fi

    export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig:$SYSROOT/lib64/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR=""   # deps already under absolute sysroot
    export AR="llvm-ar"
    export STRIP="llvm-strip"
    export RANLIB="llvm-ranlib"
    export NM="llvm-nm"
    export OBJCOPY="llvm-objcopy"

    # QEMU's configure with --cross-prefix looks for `<prefix>pkg-config`.
    # We only have plain pkg-config, so drop a shim on PATH that forwards.
    mkdir -p "$SYSROOT/bin"
    _PKGCONFIG_SHIM="$SYSROOT/bin/${ANDROID_TRIPLE}${ANDROID_API}-pkg-config"
    [ -e "$_PKGCONFIG_SHIM" ] || ln -s "$(command -v pkg-config)" "$_PKGCONFIG_SHIM"
    export PATH="$SYSROOT/bin:$PATH"

    # Bionic doesn't ship shm_open for arm64/x86_64 (only arm/i386), and
    # there's no standalone librt on Android. QEMU's meson.build:1361
    # hard-requires librt when shm_open isn't found — patch it to
    # required:false so the detection degrades gracefully.
    if grep -q "rt = cc.find_library('rt', required: true)" "$SRC_DIR/meson.build"; then
        sed -i "s|rt = cc.find_library('rt', required: true)|rt = cc.find_library('rt', required: false)|" \
            "$SRC_DIR/meson.build"
        echo "==> patched $SRC_DIR/meson.build: rt library required:false for android"
    fi

    # util/oslib-posix.c:qemu_shm_alloc() calls shm_open/shm_unlink which
    # bionic doesn't declare for arm64/x86_64. Replace the whole body on
    # Android — riscv64-softmmu with --without-default-features never
    # exercises POSIX shm at runtime.
    _OSLIB="$SRC_DIR/util/oslib-posix.c"
    if grep -q "^    fd = shm_open(shm_name->str" "$_OSLIB"; then
        sed -i '/^int qemu_shm_alloc(size_t size, Error \*\*errp)$/,/^}$/c\
int qemu_shm_alloc(size_t size, Error **errp)\
{\
    /* Android stub: bionic lacks shm_open/shm_unlink for arm64/x86_64. */\
    (void)size;\
    error_setg_errno(errp, ENOSYS, "POSIX shm not supported on this platform");\
    return -1;\
}' "$_OSLIB"
        echo "==> patched $_OSLIB: qemu_shm_alloc replaced with Android stub"
    fi

    # fsdev/9p-marshal.h declares `struct V9fsStatDotl` with members
    # st_atime_nsec / st_mtime_nsec / st_ctime_nsec (plus st_atimensec
    # variants) that collide with bionic's <sys/stat.h> macros
    # (st_atime_nsec -> st_atim.tv_nsec etc.). Undef only the _nsec
    # macros — keep st_atime/st_mtime/st_ctime defined because 9pfs code
    # (hw/9pfs/9p.c, 9p-synth.c) reads those from `struct stat`, and
    # bionic's struct stat has no st_atime member, only the macro alias.
    # No code anywhere accesses struct stat via the *_nsec names, so a
    # global undef of just those six is safe.
    _P9H="$SRC_DIR/fsdev/9p-marshal.h"
    if ! grep -q "ANDROID st_ _nsec macro undefs" "$_P9H"; then
        sed -i '/^#include "p9array.h"$/a\
\
/* ANDROID st_ _nsec macro undefs — bionic <sys/stat.h> defines these as\
 * struct-stat access-path macros, and they collide with V9fsStatDotl\
 * member names. st_atime/st_mtime/st_ctime stay defined. */\
#ifdef __ANDROID__\
# undef st_atimensec\
# undef st_mtimensec\
# undef st_ctimensec\
# undef st_atime_nsec\
# undef st_mtime_nsec\
# undef st_ctime_nsec\
#endif' "$_P9H"
        echo "==> patched $_P9H: undef bionic st_*_nsec macros"
    fi

    _EXTRA_CFLAGS="-Os -ffunction-sections -fdata-sections -I$SYSROOT/include"
    _EXTRA_CXXFLAGS="$_EXTRA_CFLAGS"
    _EXTRA_LDFLAGS="-Wl,--gc-sections -L$SYSROOT/lib"

    _CONFIGURE_ARGS+=(
        --cross-prefix="${ANDROID_TRIPLE}${ANDROID_API}-"
        --cc="$_CC"
        --cxx="$_CXX"
        --host-cc=gcc
        --enable-virtfs
        # virtfs needs attr, and bionic satisfies the attr test via in-libc
        # getxattr/setxattr (QEMU's libattr_test links without -lattr).
        --enable-attr
    )
    _STRIP_BIN="llvm-strip"
    ;;

macos-arm64|macos-x86_64)
    # Native on macOS runner. CI installs prereqs via brew
    # (ninja meson pkg-config glib).
    case "$TARGET_PLATFORM" in
        macos-arm64)  _ARCH="arm64"  ;;
        macos-x86_64) _ARCH="x86_64" ;;
    esac
    _CONFIGURE_ARGS+=(
        --enable-virtfs
        --cc=clang
        --cxx=clang++
        --extra-cflags="-arch $_ARCH"
        --extra-cxxflags="-arch $_ARCH"
    )
    _EXTRA_LDFLAGS="-Wl,-dead_strip -arch $_ARCH"
    _QEMU_BINARY_NAME="qemu-system-riscv64-unsigned"
    _QEMU_OUTPUT_NAME="qemu-system-riscv64"
    ;;

ios-arm64|ios-x86_64|tvos-arm64|tvos-x86_64)
    # Cross from macOS. The nix shell puts xcbuild's stub xcrun on PATH —
    # call Apple's /usr/bin/xcrun directly to resolve Xcode SDK paths.
    # The shell also exports DEVELOPER_DIR=<nix apple-sdk> which /usr/bin/xcrun
    # honors and would search for iphoneos/appletvos SDKs there (not present);
    # unset so xcrun falls back to `xcode-select -p` (real Xcode.app).
    # The nix apple-sdk shell also exports MACOSX_DEPLOYMENT_TARGET/SDKROOT and
    # configures `clang` on PATH as a wrapper that hard-injects
    # `-mmacos-version-min=<that target>`, which is incompatible with
    # `-mios-version-min=...`. Use Apple's /usr/bin/clang directly and strip
    # the env vars so xcrun + our -isysroot fully determine the target.
    unset DEVELOPER_DIR MACOSX_DEPLOYMENT_TARGET SDKROOT NIX_APPLE_SDK_VERSION

    # Meson also auto-detects an Objective-C compiler for Darwin targets and
    # defaults to `clang` on PATH — which is nix's wrapper. Force Apple's too.
    export OBJC=/usr/bin/clang
    export OBJCXX=/usr/bin/clang++
    case "$TARGET_PLATFORM" in
        ios-arm64)
            _SDK_NAME="iphoneos";         _ARCH="arm64"
            _MIN_FLAG="-mios-version-min=${IOS_MIN_VERSION:-15.0}"
            _MESON_CPU=aarch64
            ;;
        ios-x86_64)
            _SDK_NAME="iphonesimulator";  _ARCH="x86_64"
            _MIN_FLAG="-mios-simulator-version-min=${IOS_MIN_VERSION:-15.0}"
            _MESON_CPU=x86_64
            ;;
        tvos-arm64)
            _SDK_NAME="appletvos";        _ARCH="arm64"
            _MIN_FLAG="-mtvos-version-min=${TVOS_MIN_VERSION:-17.0}"
            _MESON_CPU=aarch64
            ;;
        tvos-x86_64)
            _SDK_NAME="appletvsimulator"; _ARCH="x86_64"
            _MIN_FLAG="-mtvos-simulator-version-min=${TVOS_MIN_VERSION:-17.0}"
            _MESON_CPU=x86_64
            ;;
    esac
    _SDK="$(/usr/bin/xcrun --sdk "$_SDK_NAME" --show-sdk-path)"
    _DARWIN_CFLAGS="-isysroot $_SDK -arch $_ARCH $_MIN_FLAG"
    case "$_ARCH" in
        x86_64) _AUTOCONF_HOST="x86_64-apple-darwin"  ;;
        arm64)  _AUTOCONF_HOST="aarch64-apple-darwin" ;;
    esac

    # Build pcre2 + libffi + glib for the iOS / tvOS sysroot. The macOS host
    # glib (brew/nix) is mach-O for 'macOS' and refuses to link into an
    # iOS-Simulator/iOS/tvOS binary with
    #     ld: building for 'iOS-simulator', but linking in object file
    #         (libglib-2.0.a built for 'macOS')
    # See poc/qemu/build-tools/build-ios-minimal.sh for the same pattern.
    PCRE2_VERSION="10.44"
    LIBFFI_VERSION="3.4.6"
    GLIB_VERSION="2.82.4"
    # zlib is consumed as a prebuilt yetty 3rdparty tarball — the nix shell
    # pulls a host x86_64 zlib in transitively (curl/python3/...) which meson
    # then hardcodes into the gio link line, breaking the cross link with
    # "found architecture 'x86_64', required architecture 'arm64'".
    ZLIB_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/zlib/version")"
    SYSROOT="$WORK_DIR/ios-sysroot-${TARGET_PLATFORM}"
    SYSROOT_STAMP="$SYSROOT/.built-$PCRE2_VERSION-$LIBFFI_VERSION-$GLIB_VERSION-zlib$ZLIB_VERSION"
    DEPS_DIR="$WORK_DIR/ios-deps-src"
    mkdir -p "$SYSROOT" "$DEPS_DIR"

    # Apple-prohibition neutralizer header. iOS / tvOS / watchOS SDKs decorate
    # functions like fork/execv/system with __attribute__((unavailable)), so
    # any TU that even *references* them fails to compile — even if the call
    # is unreachable at runtime. glib itself has dozens of fork/execvp/system
    # call sites (gbacktrace.c, gspawn-posix.c, gshell.c, ...) that we don't
    # want to patch one-by-one. Force-include this stub during the dep build
    # to make the prohibition decorators no-ops; the symbols still exist in
    # libsystem at runtime so the static .a will link.
    _IOS_STUBS_H="$SYSROOT/apple-prohibition-stubs.h"
    cat > "$_IOS_STUBS_H" <<'STUB_EOF'
/* Auto-generated by build-tools/3rdparty/qemu/_build.sh. Neutralizes the
 * __TVOS_PROHIBITED / __WATCHOS_PROHIBITED / __IOS_PROHIBITED markings so
 * fork/execv/system/etc. are merely *callable* in static-library builds.
 * Functions still exist in libsystem at runtime. */
#pragma once
#ifndef __ASSEMBLER__
#include <Availability.h>
#undef  __IOS_PROHIBITED
#define __IOS_PROHIBITED
#undef  __TVOS_PROHIBITED
#define __TVOS_PROHIBITED
#undef  __WATCHOS_PROHIBITED
#define __WATCHOS_PROHIBITED
#endif
STUB_EOF
    _DARWIN_DEP_CFLAGS="$_DARWIN_CFLAGS -include $_IOS_STUBS_H"

    _ios_fetch() {
        local url="$1" out="$2"
        if [ ! -f "$DEPS_DIR/$out" ]; then
            curl -fL --retry 3 -o "$DEPS_DIR/$out.part" "$url"
            mv "$DEPS_DIR/$out.part" "$DEPS_DIR/$out"
        fi
    }

    _ios_autotools_build() {
        local name="$1" src="$2"; shift 2
        echo "==> ios sysroot: $name"
        (
            cd "$src"
            rm -rf "build-${TARGET_PLATFORM}"
            mkdir -p "build-${TARGET_PLATFORM}"
            cd "build-${TARGET_PLATFORM}"
            CC=/usr/bin/clang CXX=/usr/bin/clang++ \
                AR=/usr/bin/ar RANLIB=/usr/bin/ranlib \
                CFLAGS="$_DARWIN_DEP_CFLAGS" \
                CXXFLAGS="$_DARWIN_DEP_CFLAGS" \
                LDFLAGS="$_DARWIN_CFLAGS" \
                ../configure --host="$_AUTOCONF_HOST" \
                             --prefix="$SYSROOT" \
                             --disable-shared --enable-static \
                             "$@"
            make -j"$NCPU"
            make install
        )
    }

    # Bypass nix's pkg-config wrapper. The wrapper unconditionally exports
    # PKG_CONFIG_PATH=$PKG_CONFIG_PATH_x86_64_apple_darwin (host zlib/glib/
    # everything), and PKG_CONFIG_PATH is searched *in addition to*
    # PKG_CONFIG_LIBDIR — so even our LIBDIR override doesn't hide host deps.
    # Extract the underlying pkg-config the wrapper exec's and call it
    # directly via a shim that scrubs PKG_CONFIG_PATH and pins LIBDIR to the
    # cross sysroot.
    _NIX_PC_WRAPPER="$(command -v pkg-config)"
    _REAL_PKGCONFIG="$(grep -oE 'exec [^ ]*-pkg-config-[^ /]*/bin/pkg-config' "$_NIX_PC_WRAPPER" | head -1 | awk '{print $2}')"
    if [ -z "$_REAL_PKGCONFIG" ] || [ ! -x "$_REAL_PKGCONFIG" ]; then
        _REAL_PKGCONFIG="$_NIX_PC_WRAPPER"  # fallback if the wrapper changes shape
    fi
    mkdir -p "$SYSROOT/bin"
    cat > "$SYSROOT/bin/sysroot-pkg-config" <<PKGCONF_SHIM_EOF
#!/bin/bash
# Auto-generated by build-tools/3rdparty/qemu/_build.sh — see comment above.
unset PKG_CONFIG_PATH
export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=""
exec "$_REAL_PKGCONFIG" "\$@"
PKGCONF_SHIM_EOF
    chmod +x "$SYSROOT/bin/sysroot-pkg-config"

    _IOS_CROSSFILE="$SYSROOT/ios-${TARGET_PLATFORM}.ini"
    cat > "$_IOS_CROSSFILE" <<IOS_CROSS_EOF
[binaries]
c          = ['/usr/bin/clang']
cpp        = ['/usr/bin/clang++']
objc       = ['/usr/bin/clang']
ar         = '/usr/bin/ar'
ranlib     = '/usr/bin/ranlib'
strip      = '/usr/bin/strip'
pkg-config = '$SYSROOT/bin/sysroot-pkg-config'

[built-in options]
c_args         = ['-arch', '$_ARCH', '-isysroot', '$_SDK', '$_MIN_FLAG', '-include', '$_IOS_STUBS_H']
cpp_args       = ['-arch', '$_ARCH', '-isysroot', '$_SDK', '$_MIN_FLAG', '-include', '$_IOS_STUBS_H']
objc_args      = ['-arch', '$_ARCH', '-isysroot', '$_SDK', '$_MIN_FLAG', '-include', '$_IOS_STUBS_H']
c_link_args    = ['-arch', '$_ARCH', '-isysroot', '$_SDK', '$_MIN_FLAG']
cpp_link_args  = ['-arch', '$_ARCH', '-isysroot', '$_SDK', '$_MIN_FLAG']
# Meson picks the objc linker for any executable that has .m sources.
# Without objc_link_args the cross flags (-arch / -isysroot / min-version)
# don't reach the link line and ld defaults to the host x86_64 → all qemu
# arm64 .o files get rejected. UIKit + Foundation also belong here, not in
# extra-ldflags, because qemu's link uses LINK_ARGS only for non-arch flags.
objc_link_args = ['-arch', '$_ARCH', '-isysroot', '$_SDK', '$_MIN_FLAG', '-framework', 'UIKit', '-framework', 'Foundation', '-framework', 'AVFoundation']

[properties]
needs_exe_wrapper  = true
have_c99_vsnprintf = true
have_c99_snprintf  = true
have_unix98_printf = true
growing_stack      = false
pkg_config_libdir  = ['$SYSROOT/lib/pkgconfig']

[host_machine]
system     = 'darwin'
cpu_family = '$_MESON_CPU'
cpu        = '$_MESON_CPU'
endian     = 'little'
IOS_CROSS_EOF

    _ios_meson_build() {
        local name="$1" src="$2"; shift 2
        echo "==> ios sysroot: $name"
        rm -rf "$DEPS_DIR/$name-build"
        meson setup "$DEPS_DIR/$name-build" "$src" \
            --cross-file="$_IOS_CROSSFILE" \
            --prefix="$SYSROOT" \
            --buildtype=release \
            --default-library=static \
            "$@"
        meson install -C "$DEPS_DIR/$name-build"
    }

    if [ -f "$SYSROOT_STAMP" ]; then
        echo "==> ios sysroot already built: $SYSROOT"
    else
        echo "==> building ios sysroot (pcre2, libffi, glib) for $TARGET_PLATFORM"

        _ios_fetch "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${PCRE2_VERSION}/pcre2-${PCRE2_VERSION}.tar.bz2" \
            "pcre2-${PCRE2_VERSION}.tar.bz2"
        [ -d "$DEPS_DIR/pcre2-${PCRE2_VERSION}" ] || \
            tar -C "$DEPS_DIR" -xjf "$DEPS_DIR/pcre2-${PCRE2_VERSION}.tar.bz2"
        # Use pcre2's cmake build (not autotools) — autotools always builds
        # pcre2grep, whose source uses execv() that __TVOS_PROHIBITED bans on
        # tvOS. CMake has PCRE2_BUILD_PCRE2GREP=OFF. glib only consumes
        # libpcre2-8 so the tools aren't needed anywhere downstream.
        echo "==> ios sysroot: pcre2 (cmake)"
        rm -rf "$DEPS_DIR/pcre2-${PCRE2_VERSION}/build-${TARGET_PLATFORM}"
        cmake -S "$DEPS_DIR/pcre2-${PCRE2_VERSION}" \
              -B "$DEPS_DIR/pcre2-${PCRE2_VERSION}/build-${TARGET_PLATFORM}" \
              -G Ninja \
              -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_INSTALL_PREFIX="$SYSROOT" \
              -DCMAKE_C_COMPILER=/usr/bin/clang \
              -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
              -DCMAKE_C_FLAGS="$_DARWIN_DEP_CFLAGS" \
              -DCMAKE_CXX_FLAGS="$_DARWIN_DEP_CFLAGS" \
              -DCMAKE_SYSTEM_NAME=Darwin \
              -DCMAKE_OSX_SYSROOT="$_SDK" \
              -DCMAKE_OSX_ARCHITECTURES="$_ARCH" \
              -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
              -DBUILD_SHARED_LIBS=OFF \
              -DPCRE2_BUILD_PCRE2GREP=OFF \
              -DPCRE2_BUILD_TESTS=OFF
        cmake --build  "$DEPS_DIR/pcre2-${PCRE2_VERSION}/build-${TARGET_PLATFORM}" -j"$NCPU"
        cmake --install "$DEPS_DIR/pcre2-${PCRE2_VERSION}/build-${TARGET_PLATFORM}"

        _ios_fetch "https://github.com/libffi/libffi/releases/download/v${LIBFFI_VERSION}/libffi-${LIBFFI_VERSION}.tar.gz" \
            "libffi-${LIBFFI_VERSION}.tar.gz"
        [ -d "$DEPS_DIR/libffi-${LIBFFI_VERSION}" ] || \
            tar -C "$DEPS_DIR" -xzf "$DEPS_DIR/libffi-${LIBFFI_VERSION}.tar.gz"
        # src/aarch64/sysv.S emits cfi_def_cfa / cfi_adjust_cfa_offset directives
        # that Apple clang's integrated assembler (Xcode 16.4) rejects with
        # "invalid CFI advance_loc expression" — it can't compute the implicit
        # advance between CFI ops in this Mach-O section layout. Override the
        # autoconf cache so HAVE_AS_CFI_PSEUDO_OP stays undefined and libffi's
        # cfi_* macros expand to nothing. Only DWARF unwind metadata is lost,
        # which is irrelevant for a static lib statically linked into qemu.
        (
            export gcc_cv_as_cfi_pseudo_op=no
            _ios_autotools_build "libffi" "$DEPS_DIR/libffi-${LIBFFI_VERSION}"
        )

        # Fetch the prebuilt zlib tarball published by build-3rdparty-zlib.yml
        # for $TARGET_PLATFORM and unpack it into the sysroot. Without an
        # arm64 zlib visible to meson here, glib's gio links the nix shell's
        # transitive host x86_64 zlib and the cross link aborts.
        _ZLIB_URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"
        _ZLIB_URL="$_ZLIB_URL_BASE/lib-zlib-${ZLIB_VERSION}/zlib-${TARGET_PLATFORM}-${ZLIB_VERSION}.tar.gz"
        _ios_fetch "$_ZLIB_URL" "zlib-${TARGET_PLATFORM}-${ZLIB_VERSION}.tar.gz"
        echo "==> ios sysroot: zlib (prebuilt $TARGET_PLATFORM-${ZLIB_VERSION})"
        tar -C "$SYSROOT" -xzf "$DEPS_DIR/zlib-${TARGET_PLATFORM}-${ZLIB_VERSION}.tar.gz"
        # Synthesize a zlib.pc so meson's `dependency('zlib')` lands on the
        # sysroot .a; otherwise the nix-wrapped pkg-config still surfaces the
        # host zlib via PKG_CONFIG_PATH_FOR_TARGET despite our LIBDIR override.
        mkdir -p "$SYSROOT/lib/pkgconfig"
        cat > "$SYSROOT/lib/pkgconfig/zlib.pc" <<ZLIB_PC_EOF
prefix=$SYSROOT
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: zlib
Description: zlib (yetty prebuilt for $TARGET_PLATFORM)
Version: ${ZLIB_VERSION}
Libs: -L\${libdir} -lz
Cflags: -I\${includedir}
ZLIB_PC_EOF

        GLIB_MINOR="${GLIB_VERSION%.*}"
        _ios_fetch "https://download.gnome.org/sources/glib/${GLIB_MINOR}/glib-${GLIB_VERSION}.tar.xz" \
            "glib-${GLIB_VERSION}.tar.xz"
        [ -d "$DEPS_DIR/glib-${GLIB_VERSION}" ] || \
            tar -C "$DEPS_DIR" -xJf "$DEPS_DIR/glib-${GLIB_VERSION}.tar.xz"
        export PKG_CONFIG_PATH="$SYSROOT/lib/pkgconfig"
        export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig"
        export PKG_CONFIG_SYSROOT_DIR=""
        _ios_meson_build "glib" "$DEPS_DIR/glib-${GLIB_VERSION}" \
            -Dtests=false \
            -Dintrospection=disabled \
            -Dnls=disabled \
            -Dlibmount=disabled \
            -Dglib_debug=disabled \
            -Dlibelf=disabled \
            -Dsysprof=disabled

        touch "$SYSROOT_STAMP"
        echo "==> ios sysroot ready: $SYSROOT"
    fi

    export PKG_CONFIG_PATH="$SYSROOT/lib/pkgconfig"
    export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR=""
    # QEMU's configure honors $PKG_CONFIG and forwards it into its meson run.
    # Without this, qemu's meson uses the bare 'pkg-config' on PATH (nix's
    # wrapper), which surfaces host x86_64 zlib/glib regardless of LIBDIR.
    export PKG_CONFIG="$SYSROOT/bin/sysroot-pkg-config"

    # ------------------------------------------------------------------
    # Drop the iOS / tvOS UIKit harness (canonical copy lives at
    # build-tools/ios-tvos/yetty-qemu/tvos-main.m — same file driven
    # through xcodegen for provisioning) and apply the qemu source
    # patches from build-tools/3rdparty/qemu/patches/.
    # The patches:
    #   0001 — system/main.c: rename main → yetty_qemu_main
    #   0002 — include/qemu/osdep.h: gate pthread_jit_write_protect_np
    #   0003 — block/file-posix.c: always include <sys/mount.h> on Apple
    #   0004 — tcg/region.c: skip MAP_JIT under CONFIG_TCG_INTERPRETER
    #   0005 — meson.build: add system/tvos-main.m + link_language='c'
    # `patch -p1 -N --silent` is idempotent (re-applying = no-op exit 1
    # which we ignore via `|| true`).
    # ------------------------------------------------------------------
    _TVOS_MAIN_SRC="$REPO_ROOT/build-tools/ios-tvos/yetty-qemu/tvos-main.m"
    [ -f "$_TVOS_MAIN_SRC" ] || { echo "missing $_TVOS_MAIN_SRC" >&2; exit 1; }
    cp "$_TVOS_MAIN_SRC" "$SRC_DIR/system/tvos-main.m"
    for _patch in "$SCRIPT_DIR"/patches/*.patch; do
        echo "==> applying $(basename "$_patch")"
        ( cd "$SRC_DIR" && patch -p1 -N --silent < "$_patch" ) || true
    done


    _CONFIGURE_ARGS+=(
        --enable-virtfs
        # tvOS / iOS sandbox forbids mmap(PROT_WRITE|PROT_EXEC) — there's no
        # JIT entitlement for sideloaded apps. Force TCG into interpreter
        # mode (TCI) so qemu doesn't try to allocate an executable code-gen
        # buffer at startup. Slower than JIT, but the only thing that runs
        # in the iOS/tvOS sandbox.
        --enable-tcg-interpreter
        --cc=/usr/bin/clang
        --cxx=/usr/bin/clang++
        --objcc=/usr/bin/clang
        --host-cc=/usr/bin/clang
        # --cross-prefix forces QEMU's configure into cross_compile=yes mode,
        # which writes a [host_machine] section into config-meson.cross. That
        # in turn makes meson skip the run-time sanity check (impossible on a
        # device build, and broken on iOS-Simulator since the binary needs
        # DYLD_ROOT_PATH that's not set in the build env). Empty value is fine —
        # we already pass --cc/--cxx/--objcc to locate the compilers.
        --cross-prefix=""
        # _IOS_STUBS_H neutralizes __IOS_PROHIBITED / __TVOS_PROHIBITED on the
        # function decls QEMU's util/* + others need (sigaltstack, etc.) —
        # otherwise tvos-arm64 fails on util_coroutine-sigaltstack.c, and
        # ios-arm64 would on any future TU that touches a similarly marked
        # symbol. The functions exist in libsystem at runtime; only the
        # SDK header attribute blocks the compile.
        --extra-cflags="$_DARWIN_CFLAGS -include $_IOS_STUBS_H -I$SYSROOT/include"
        --extra-cxxflags="$_DARWIN_CFLAGS -include $_IOS_STUBS_H -I$SYSROOT/include"
        --extra-objcflags="$_DARWIN_CFLAGS -include $_IOS_STUBS_H"
    )
    # UIKit + Foundation pull in the tvos-main.m harness's runtime. UIKit is
    # available on iOS, iOS-Simulator, tvOS and tvOS-Simulator SDKs.
    _EXTRA_LDFLAGS="-Wl,-dead_strip $_DARWIN_CFLAGS -L$SYSROOT/lib -framework UIKit -framework Foundation -framework AVFoundation"
    _QEMU_BINARY_NAME="qemu-system-riscv64-unsigned"
    _QEMU_OUTPUT_NAME="qemu-system-riscv64"
    ;;

windows-x86_64)
    # Windows uses MSYS2 CLANG64 (clang + lld + mingw-w64 libs). Caller is
    # expected to be inside the CLANG64 environment with these packages:
    #   mingw-w64-clang-x86_64-{clang,lld,glib2,libslirp,zlib,
    #                          ninja,meson,pkgconf,python}
    #   git diffutils
    # CI sets this up via msys2/setup-msys2 in build-3rdparty-qemu.yml.
    if [ "${MSYSTEM:-}" != "CLANG64" ]; then
        echo "error: windows-x86_64 must run inside MSYS2 CLANG64 (MSYSTEM=$MSYSTEM)" >&2
        exit 1
    fi

    # QEMU's symlink-install-tree.py creates a staging tree of symlinks for
    # `meson install`. On Windows without Developer Mode the symlink calls
    # fail and abort meson setup even though we never run install. Replace
    # it with a no-op.
    cat > "$SRC_DIR/scripts/symlink-install-tree.py" <<'PYEOF'
#!/usr/bin/env python3
import sys
sys.exit(0)
PYEOF

    # Static-link libslirp on Windows: the system mingw libslirp ships both
    # libslirp.a and libslirp.dll.a, and lld picks the import lib by default.
    # An overlay slirp.pc points pkg-config at the .a, lists slirp's private
    # deps (-liconv -liphlpapi -lws2_32) in Libs, and adds -DLIBSLIRP_STATIC
    # to Cflags so the headers don't emit __declspec(dllimport). Result:
    # libslirp folded into qemu-system-riscv64.exe — no libslirp-0.dll to
    # bundle next to it.
    #
    # The prefix is baked as an absolute Windows path via cygpath because
    # pkgconf only auto-rewrites ${prefix} when the .pc file lives under
    # <prefix>/lib/pkgconfig/, which our overlay doesn't.
    _STATICPC_DIR="$WORK_DIR/static-pc-${TARGET_PLATFORM}"
    _CLANG64_WIN="$(cygpath -m /clang64)"
    mkdir -p "$_STATICPC_DIR"
    cat > "$_STATICPC_DIR/slirp.pc" <<PCEOF
prefix=$_CLANG64_WIN
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: slirp
Description: User-space network stack (static overlay for QEMU on MSYS2 CLANG64)
Version: 4.9.1
Requires: glib-2.0
Libs: \${libdir}/libslirp.a -liconv -liphlpapi -lws2_32
Cflags: -I\${includedir}/slirp -DLIBSLIRP_STATIC
PCEOF
    export PKG_CONFIG_PATH="$_STATICPC_DIR:${PKG_CONFIG_PATH:-}"

    _CONFIGURE_ARGS+=(
        --cc=clang
        --cxx=clang++
    )
    # clang 22.x on mingw-w64 hits an LLVM ICE in DwarfDebug::emitDebugLocImpl
    # while compiling util/oslib-win32.c with debug info. We don't ship
    # symbols anyway — turn debug info off (also strips ~25% off the .exe).
    _EXTRA_CFLAGS="-Os -g0 -ffunction-sections -fdata-sections"
    _EXTRA_CXXFLAGS="-Os -g0 -ffunction-sections -fdata-sections"
    _EXTRA_LDFLAGS="-Wl,--gc-sections,-s"
    _QEMU_BINARY_NAME="qemu-system-riscv64.exe"
    ;;

*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

#-----------------------------------------------------------------------------
# Append compiler/linker extra flags and force bundled slirp to static.
#-----------------------------------------------------------------------------
_CONFIGURE_ARGS+=(
    --extra-cflags="$_EXTRA_CFLAGS"
    --extra-cxxflags="$_EXTRA_CXXFLAGS"
    --extra-ldflags="$_EXTRA_LDFLAGS"
)
# libslirp is the one dep end-user Linuxes can't be relied on to have —
# pull it into the qemu binary instead of shipping subprojects/slirp.so.
_CONFIGURE_ARGS+=(-Dslirp:default_library=static)

#-----------------------------------------------------------------------------
# Configure + build
#-----------------------------------------------------------------------------
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==> configuring QEMU for $TARGET_PLATFORM"
"$SRC_DIR/configure" "${_CONFIGURE_ARGS[@]}"

echo "==> building (-j${NCPU})"
# QEMU's Makefile is a thin wrapper around ninja; on MSYS2 CLANG64 we don't
# install GNU make (not in the clang-x86_64 package set), so call ninja
# directly. Other platforms keep using make so any Makefile-only targets
# (e.g. the kvm/headers targets) still resolve.
if [ "${MSYSTEM:-}" = "CLANG64" ]; then
    ninja -j"$NCPU"
else
    make -j"$NCPU"
fi

BUILT="$BUILD_DIR/$_QEMU_BINARY_NAME"
[ -f "$BUILT" ] || { echo "missing binary: $BUILT" >&2; exit 1; }

if command -v "$_STRIP_BIN" >/dev/null 2>&1; then
    "$_STRIP_BIN" "$BUILT" || true
fi

#-----------------------------------------------------------------------------
# Stage + package
#-----------------------------------------------------------------------------
rm -rf "$STAGE"
mkdir -p "$STAGE"

OUT_NAME="${_QEMU_OUTPUT_NAME:-$_QEMU_BINARY_NAME}"
cp "$BUILT" "$STAGE/$OUT_NAME"

# Windows: bundle every non-system DLL the .exe links against. Without
# these the binary won't start outside an MSYS2 CLANG64 shell. libslirp is
# now folded into the .exe via the static overlay above, so its DLL is no
# longer staged.
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    _CLANG64_BIN="/clang64/bin"
    for _dll in libglib-2.0-0.dll libintl-8.dll libiconv-2.dll \
                libpcre2-8-0.dll zlib1.dll \
                libwinpthread-1.dll libc++.dll libunwind.dll; do
        if [ -f "$_CLANG64_BIN/$_dll" ]; then
            cp "$_CLANG64_BIN/$_dll" "$STAGE/"
        fi
    done
fi

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "QEMU asset ready ($TARGET_PLATFORM):"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
