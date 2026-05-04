#!/bin/bash
# Build Linux kernel for RISC-V64 + bundle Alpine minirootfs, producing
# linux-${VERSION}.tar.gz containing:
#   kernel-riscv64.bin
#   alpine-rootfs/...  (with /init pre-written for console boot)
#
# Env vars:
#   VERSION         derived — read from ./version, used in output filename
#   OUTPUT_DIR      required — where to place the tarball
#   REPO_ROOT       optional — yetty checkout root (default: ../../.. from this script)
#   WORK_DIR        optional — intermediate build tree (default: /tmp/yetty-asset-linux)
#   CROSS_COMPILE   optional — toolchain prefix (default: riscv64-unknown-linux-gnu-)
#
# The `version` file format is <upstream>-<pkg-rev>, e.g. `7.0-1` — single
# source of truth for both the upstream kernel tag fetched here AND the
# lib-linux-<version> release tag / linux-<version>.tar.gz tarball name.
# Bump <pkg-rev> for packaging-only changes (e.g. bundled alpine bump,
# kernel config tweaks); bump <upstream> when moving to a new kernel.
#
# Hermetic Linux build: needs make, RISC-V cross-toolchain, bc, bison,
# flex, libssl-dev, libelf-dev, cpio, rsync, curl, tar.

set -euo pipefail

VERSION_FILE="$(dirname "$0")/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

# Split <upstream>-<pkg-rev>. Upstream tags themselves can contain `-`
# (e.g. 7.0-rc5), so split off the last `-` component as the pkg rev.
LINUX_VERSION="${VERSION%-*}"
PKG_REV="${VERSION##*-}"
[ "$LINUX_VERSION" != "$VERSION" ] && [ -n "$PKG_REV" ] || {
    echo "$VERSION_FILE: expected <upstream>-<rev>, got '$VERSION'" >&2
    exit 1
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "$SCRIPT_DIR/../../.." && pwd)}"
WORK_DIR="${WORK_DIR:-/tmp/yetty-asset-linux}"
# Alpine minirootfs is incidental to the kernel asset (only used as a 9p
# rootfs for tinyemu). Bumping it bumps the pkg rev, not the upstream
# kernel version. Pinned here on purpose.
ALPINE_VERSION="3.23"
ALPINE_RELEASE="3.23.4"
CROSS_COMPILE="${CROSS_COMPILE:-riscv64-unknown-linux-gnu-}"

KERNEL_CONFIG="$REPO_ROOT/poc/qemu/configs/linux-kernel-${LINUX_VERSION}.config"
[ -f "$KERNEL_CONFIG" ] || { echo "missing kernel config: $KERNEL_CONFIG" >&2; exit 1; }

NCPU="$(nproc 2>/dev/null || echo 4)"

KERNEL_URL="https://github.com/torvalds/linux/archive/refs/tags/v${LINUX_VERSION}.tar.gz"
ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION}/releases/riscv64/alpine-minirootfs-${ALPINE_RELEASE}-riscv64.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR"
cd "$WORK_DIR"

#-----------------------------------------------------------------------------
# Kernel
#-----------------------------------------------------------------------------

if [ ! -f "linux-${LINUX_VERSION}.tar.gz" ]; then
    echo "==> downloading Linux ${LINUX_VERSION}"
    curl -fL --retry 3 -o "linux-${LINUX_VERSION}.tar.gz" "$KERNEL_URL"
fi

if [ ! -d "linux-${LINUX_VERSION}" ]; then
    echo "==> extracting kernel"
    tar xf "linux-${LINUX_VERSION}.tar.gz"
fi

cp "$KERNEL_CONFIG" "linux-${LINUX_VERSION}/.config"

echo "==> building kernel (CROSS_COMPILE=${CROSS_COMPILE}, -j${NCPU})"
make -C "linux-${LINUX_VERSION}" \
    ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" \
    -j"$NCPU" olddefconfig Image

KERNEL_IMAGE="linux-${LINUX_VERSION}/arch/riscv/boot/Image"
[ -f "$KERNEL_IMAGE" ] || { echo "missing $KERNEL_IMAGE" >&2; exit 1; }

#-----------------------------------------------------------------------------
# Alpine minirootfs
#-----------------------------------------------------------------------------

if [ ! -f "alpine-minirootfs-${ALPINE_RELEASE}-riscv64.tar.gz" ]; then
    echo "==> downloading Alpine ${ALPINE_RELEASE} minirootfs"
    curl -fL --retry 3 -o "alpine-minirootfs-${ALPINE_RELEASE}-riscv64.tar.gz" "$ALPINE_URL"
fi

ROOTFS_DIR="$WORK_DIR/alpine-rootfs"
if [ ! -f "$ROOTFS_DIR/bin/busybox" ]; then
    echo "==> extracting Alpine rootfs"
    rm -rf "$ROOTFS_DIR"
    mkdir -p "$ROOTFS_DIR"
    tar -C "$ROOTFS_DIR" -xzf "alpine-minirootfs-${ALPINE_RELEASE}-riscv64.tar.gz"
fi

# /init for TinyEMU / QEMU console boot (slirp: 10.0.2.x user-mode net)
cat > "$ROOTFS_DIR/init" << 'INIT_EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null || true
hostname tinyemu

ip link set lo up
ip link set eth0 up 2>/dev/null
ip addr add 10.0.2.15/24 dev eth0 2>/dev/null
ip route add default via 10.0.2.2 2>/dev/null
echo "nameserver 10.0.2.3" > /etc/resolv.conf

exec /bin/sh
INIT_EOF
chmod 755 "$ROOTFS_DIR/init"

#-----------------------------------------------------------------------------
# Stage + package
#-----------------------------------------------------------------------------

STAGE="$WORK_DIR/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"

# Brotli q11 the kernel — embed pipeline picks it up pre-compressed,
# avoids a recompress on every yetty configure. Runtime path mode
# (tinyemu_copy_runtime_to_bundle) gets a decompressed copy side-by-side
# at consumer-side fetch time (3rdparty-fetch.cmake).
: "${BROTLI_QUALITY:=11}"
echo "==> brotli kernel-riscv64.bin (quality $BROTLI_QUALITY)"
in_size="$(stat -c%s "$KERNEL_IMAGE" 2>/dev/null || stat -f%z "$KERNEL_IMAGE")"
brotli -q "$BROTLI_QUALITY" -f -o "$STAGE/kernel-riscv64.bin.br" "$KERNEL_IMAGE"
out_size="$(stat -c%s "$STAGE/kernel-riscv64.bin.br" 2>/dev/null || stat -f%z "$STAGE/kernel-riscv64.bin.br")"
printf "    kernel-riscv64.bin  %10d -> %10d bytes\n" "$in_size" "$out_size"

# alpine-rootfs/ stays raw — only used by --qemu virtio-9p at runtime,
# never embedded into the yetty binary, so no compression win.
# rsync preserves perms/symlinks; skip /dev so device nodes don't leak
rsync -a --delete --exclude='dev/*' "$ROOTFS_DIR/" "$STAGE/alpine-rootfs/"
mkdir -p "$STAGE/alpine-rootfs/dev"

TARBALL="$OUTPUT_DIR/linux-${VERSION}.tar.gz"
echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "Linux asset ready:"
ls -lh "$TARBALL"
ENTRIES="$(tar -tzf "$TARBALL" | wc -l)"
echo "contents (first 20 of $ENTRIES):"
tar -tzf "$TARBALL" | sed -n '1,20p'
