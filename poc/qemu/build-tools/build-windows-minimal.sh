#!/bin/bash
# Bash port of build-windows-minimal.ps1 — builds qemu-system-riscv64.exe
# under MSYS2 CLANG64. Run from any MSYS2 subsystem (MSYS, MINGW64, CLANG64).
# The script re-execs the build step in CLANG64 so PATH/pkg-config are set up
# for the clang+lld+mingw-w64 prefix that QEMU's meson.build expects.
#
# Assumes MSYS2 is already installed at /c/msys64 with the CLANG64 toolchain
# (clang, lld, glib2, pixman, zlib, ninja, meson, pkgconf, python, git,
# diffutils). The .ps1 script handles bootstrap; this .sh assumes ready env.

set -e

SCRIPTDIR="$(cd "$(dirname "$0")" && pwd)"
REPOROOT="$(cd "$SCRIPTDIR/../../.." && pwd)"
BUILDDIR="$REPOROOT/build-windows-minimal"
LOGDIR="$REPOROOT/tmp"
QEMU_VERSION="11.0.0-rc4"
QEMUSRC="$SCRIPTDIR/qemu-${QEMU_VERSION}"
QEMUTAR="$SCRIPTDIR/qemu-${QEMU_VERSION}.tar.xz"
QEMU_URL="https://download.qemu.org/qemu-${QEMU_VERSION}.tar.xz"

MSYS2_ROOT="/c/msys64"
BASH_EXE="$MSYS2_ROOT/usr/bin/bash.exe"
CLANG64_BIN="$MSYS2_ROOT/clang64/bin"

NCPU="${NCPU:-$(nproc 2>/dev/null || echo 4)}"

mkdir -p "$LOGDIR"

# -- 1. Verify MSYS2 CLANG64 toolchain ---------------------------------------
if [ ! -x "$BASH_EXE" ]; then
    echo "ERROR: MSYS2 bash not found at $BASH_EXE" >&2
    echo "Run build-windows-minimal.ps1 once to bootstrap MSYS2." >&2
    exit 1
fi
if [ ! -x "$CLANG64_BIN/clang.exe" ]; then
    echo "ERROR: CLANG64 toolchain missing ($CLANG64_BIN/clang.exe)" >&2
    echo "Install: pacman -S --needed mingw-w64-clang-x86_64-{clang,lld,glib2,pixman,zlib,ninja,meson,pkgconf,python} git diffutils" >&2
    exit 1
fi

# -- 2. Download & extract QEMU source ---------------------------------------
if [ ! -d "$QEMUSRC" ]; then
    if [ ! -f "$QEMUTAR" ]; then
        echo "Downloading QEMU $QEMU_VERSION..."
        curl -L --fail -o "$QEMUTAR" "$QEMU_URL"
    fi
    echo "Extracting $QEMUTAR (excluding symlink-heavy subtrees)..."
    # Same exclusions as the .ps1: MSYS2 tar can't create symlinks on stock
    # Windows (no Developer Mode / admin), and none of these are needed for a
    # riscv64-softmmu-only build.
    tar -xf "$QEMUTAR" -C "$SCRIPTDIR" \
        --exclude="qemu-${QEMU_VERSION}/roms/u-boot" \
        --exclude="qemu-${QEMU_VERSION}/roms/u-boot-sam460ex" \
        --exclude="qemu-${QEMU_VERSION}/roms/edk2" \
        --exclude="qemu-${QEMU_VERSION}/roms/skiboot" \
        --exclude="qemu-${QEMU_VERSION}/roms/openbios" \
        --exclude="qemu-${QEMU_VERSION}/roms/SLOF" \
        --exclude="qemu-${QEMU_VERSION}/roms/seabios" \
        --exclude="qemu-${QEMU_VERSION}/roms/seabios-hppa" \
        --exclude="qemu-${QEMU_VERSION}/roms/vof" \
        --exclude="qemu-${QEMU_VERSION}/roms/opensbi" \
        --exclude="qemu-${QEMU_VERSION}/roms/qboot" \
        --exclude="qemu-${QEMU_VERSION}/roms/QemuMacDrivers" \
        --exclude="qemu-${QEMU_VERSION}/roms/npcm7xx_bootrom" \
        --exclude="qemu-${QEMU_VERSION}/roms/sgabios" \
        --exclude="qemu-${QEMU_VERSION}/roms/ipxe" \
        --exclude="qemu-${QEMU_VERSION}/tests/lcitool" \
        --exclude="qemu-${QEMU_VERSION}/rust" \
        --exclude="qemu-${QEMU_VERSION}/subprojects/libvduse" \
        --exclude="qemu-${QEMU_VERSION}/subprojects/libvhost-user"

    # We excluded rust/ above but Kconfig still references symbols defined
    # under rust/. Re-extract just the four Kconfig files (no symlinks).
    tar -xf "$QEMUTAR" -C "$SCRIPTDIR" --overwrite \
        "qemu-${QEMU_VERSION}/rust/Kconfig" \
        "qemu-${QEMU_VERSION}/rust/hw/Kconfig" \
        "qemu-${QEMU_VERSION}/rust/hw/char/Kconfig" \
        "qemu-${QEMU_VERSION}/rust/hw/timer/Kconfig"
fi

# -- 3. Drop in the minimal device config ------------------------------------
mkdir -p "$QEMUSRC/configs/devices/riscv64-softmmu"
cp "$SCRIPTDIR/../configs/riscv64-softmmu/default.mak" \
   "$QEMUSRC/configs/devices/riscv64-softmmu/"

# -- 3b. Generate static-link slirp.pc overlay -------------------------------
# Override the system slirp.pc so pkg-config hands meson the static archive
# (libslirp.a) plus its private deps (-liconv -liphlpapi -lws2_32) and defines
# LIBSLIRP_STATIC so the libslirp headers don't emit __declspec(dllimport).
# Result: qemu-system-riscv64.exe with no libslirp-0.dll runtime dependency.
#
# We bake the absolute Windows prefix in (via cygpath) instead of /clang64,
# because pkgconf's auto prefix-rewrite only fires when the .pc file lives
# under <prefix>/lib/pkgconfig/, which our overlay doesn't.
STATICPC_DIR="$SCRIPTDIR/static-pc"
CLANG64_WINPATH="$(cygpath -m "$MSYS2_ROOT/clang64")"
mkdir -p "$STATICPC_DIR"
cat > "$STATICPC_DIR/slirp.pc" <<PCEOF
prefix=$CLANG64_WINPATH
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: slirp
Description: User-space network stack (static overlay for QEMU on MSYS2 CLANG64)
Version: 4.9.1
Requires: glib-2.0
Libs: \${libdir}/libslirp.a -liconv -liphlpapi -lws2_32
Cflags: -I\${includedir}/slirp -DLIBSLIRP_STATIC
PCEOF

# -- 4. Neutralise scripts/symlink-install-tree.py ---------------------------
# Postconf runs this to stage `meson install` symlinks. On Windows without
# Developer Mode it fails and breaks meson setup. We never run `meson install`.
cat > "$QEMUSRC/scripts/symlink-install-tree.py" <<'PYEOF'
#!/usr/bin/env python3
# yetty-windows: no-op -- we don't run `meson install`, so skip the symlink
# staging entirely (it fails on Windows without Developer Mode).
import sys
sys.exit(0)
PYEOF

# -- 5. Build dir ------------------------------------------------------------
# Reuse existing build dir for incremental ninja rebuilds. Set YETTY_CLEAN=1
# (or rm the dir) to force a from-scratch rebuild.
if [ -f "$BUILDDIR/qemu-system-riscv64.exe" ] && [ -z "$YETTY_CLEAN" ]; then
    echo "Reusing existing build at $BUILDDIR (YETTY_CLEAN=1 to wipe)"
    SKIP_BUILD=0  # ninja still re-checks; just don't wipe
else
    rm -rf "$BUILDDIR"
    mkdir -p "$BUILDDIR"
fi

# -- 6. Configure + build inside MSYS2 CLANG64 -------------------------------
# Re-exec the actual build under bash -l with MSYSTEM=CLANG64 so /clang64/bin
# is on PATH and pkg-config points at the clang64 prefix. This is the
# QEMU-supported Windows toolchain per its meson.build error message.
QEMUSRC_M="$(cygpath -u "$QEMUSRC")"
BUILDDIR_M="$(cygpath -u "$BUILDDIR")"
LOGDIR_M="$(cygpath -u "$LOGDIR")"
STATIC_PC_M="$(cygpath -u "$SCRIPTDIR/static-pc")"

BUILD_SCRIPT="$LOGDIR/run-build-windows-minimal.sh"
cat > "$BUILD_SCRIPT" <<EOF
#!/usr/bin/env bash
set -e
cd "$BUILDDIR_M"

# Static-link libslirp: overlay slirp.pc points pkg-config at libslirp.a +
# defines LIBSLIRP_STATIC so the headers don't emit __declspec(dllimport).
export PKG_CONFIG_PATH="$STATIC_PC_M:\${PKG_CONFIG_PATH:-}"

echo "=== toolchain ==="
which clang && clang --version | head -1
which ld.lld || true
which meson  && meson --version
which ninja  && ninja --version
which pkgconf && pkgconf --version

echo "=== configure (with slirp) ==="
"$QEMUSRC_M/configure" \\
    --target-list=riscv64-softmmu \\
    --without-default-features \\
    --enable-tcg \\
    --enable-fdt=internal \\
    --enable-slirp \\
    --disable-werror \\
    --disable-docs \\
    --disable-guest-agent \\
    --disable-tools \\
    --disable-qom-cast-debug \\
    --disable-coroutine-pool \\
    --cc=clang \\
    --cxx=clang++ \\
    --extra-cflags='-Os -g0 -ffunction-sections -fdata-sections' \\
    --extra-cxxflags='-Os -g0 -ffunction-sections -fdata-sections' \\
    --extra-ldflags='-Wl,--gc-sections,-s' \\
    > "$LOGDIR_M/configure-windows-minimal.log" 2>&1

echo "=== build ($NCPU jobs) ==="
ninja -j $NCPU > "$LOGDIR_M/build-windows-minimal.log" 2>&1

echo "=== done ==="
EOF
chmod +x "$BUILD_SCRIPT"

echo "Running build inside MSYS2 CLANG64..."
BUILD_SCRIPT_M="$(cygpath -u "$BUILD_SCRIPT")"
if ! MSYSTEM=CLANG64 CHERE_INVOKING=1 "$BASH_EXE" -lc "bash '$BUILD_SCRIPT_M'"; then
    echo "Build failed. configure log tail:" >&2
    tail -n 40 "$LOGDIR/configure-windows-minimal.log" 2>/dev/null >&2 || true
    echo "--- build log tail ---" >&2
    tail -n 40 "$LOGDIR/build-windows-minimal.log" 2>/dev/null >&2 || true
    exit 1
fi

# -- 7. Stage runtime DLLs next to the exe ------------------------------------
BINARY="$BUILDDIR/qemu-system-riscv64.exe"
echo ""
echo "Build complete:"
ls -lh "$BINARY"

DLLS=(
    libglib-2.0-0.dll libintl-8.dll libiconv-2.dll
    libpcre2-8-0.dll libpixman-1-0.dll zlib1.dll
    libwinpthread-1.dll libc++.dll libunwind.dll
)
for dll in "${DLLS[@]}"; do
    if [ -f "$CLANG64_BIN/$dll" ]; then
        cp -f "$CLANG64_BIN/$dll" "$BUILDDIR/"
    fi
done

echo ""
echo "qemu binary: $BINARY"
echo "Run yetty cmake (e.g. .\\build-tools\\windows\\build.bat) to fetch"
echo "assets and produce the final yetty.exe."
