#!/bin/bash
# Build a raw ext4 disk image from Alpine minirootfs for tinyemu virtio-blk.
# Produces alpine-disk-${VERSION}.tar.gz containing alpine-rootfs.img.
#
# Image is sized at 2x the uncompressed rootfs (64 MiB floor) to leave
# headroom for first-boot init and package state. -E resize=4G reserves
# GDT slots so a later resize2fs up to 4 GB stays online (no offline
# metadata rebuild). The actual grow-on-first-launch is a follow-up step.
#
# Env vars:
#   VERSION         derived — read from ./version, used in output filename
#   OUTPUT_DIR      required — where to place the tarball
#   WORK_DIR        optional — intermediate build tree (default: /tmp/yetty-asset-alpine-disk)
#
# The `version` file format is <alpine-release>-<arch>-<pkg-rev>, e.g.
# `3.23.4-riscv64-1`. The whole string is the lib-tag suffix and tarball
# version; the upstream Alpine release + arch are parsed out of it for
# the minirootfs fetch URL. Bump <pkg-rev> for packaging-only changes
# (e.g. /init tweaks); bump <alpine-release> for an Alpine point release.
#
# Needs: curl, tar, gzip, e2fsprogs, util-linux, and passwordless sudo
# (for losetup/mount/umount). GitHub-hosted ubuntu-latest runners
# satisfy this out of the box.

set -euo pipefail

VERSION_FILE="$(dirname "$0")/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

# Split <alpine-release>-<arch>-<pkg-rev>. e.g. 3.23.4-riscv64-1 →
# release=3.23.4, arch=riscv64, rev=1. Two splits off the right.
PKG_REV="${VERSION##*-}"
_UPSTREAM="${VERSION%-*}"
ALPINE_ARCH="${_UPSTREAM##*-}"
ALPINE_RELEASE="${_UPSTREAM%-*}"
[ "$ALPINE_RELEASE" != "$_UPSTREAM" ] \
    && [ "$_UPSTREAM" != "$VERSION" ] \
    && [ -n "$PKG_REV" ] \
    && [ -n "$ALPINE_ARCH" ] \
    || {
    echo "$VERSION_FILE: expected <alpine-release>-<arch>-<rev>, got '$VERSION'" >&2
    exit 1
}
# Alpine minor (3.23) is the dirname under /alpine/v<minor>/releases/.
ALPINE_VERSION="${ALPINE_RELEASE%.*}"

WORK_DIR="${WORK_DIR:-/tmp/yetty-asset-alpine-disk}"

ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION}/releases/${ALPINE_ARCH}/alpine-minirootfs-${ALPINE_RELEASE}-${ALPINE_ARCH}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR"
cd "$WORK_DIR"

#-----------------------------------------------------------------------------
# Download minirootfs
#-----------------------------------------------------------------------------

TARBALL="alpine-minirootfs-${ALPINE_RELEASE}-${ALPINE_ARCH}.tar.gz"
if [ ! -f "$TARBALL" ]; then
    echo "==> downloading Alpine ${ALPINE_RELEASE} minirootfs (${ALPINE_ARCH})"
    curl -fL --retry 3 -o "$TARBALL" "$ALPINE_URL"
fi

#-----------------------------------------------------------------------------
# Compute image size: 2x uncompressed rootfs, MiB-rounded, 64 MiB floor.
# zcat | wc -c is unambiguous (avoids gzip -l header parsing) and the
# minirootfs is small enough that the extra read costs nothing.
#-----------------------------------------------------------------------------

RAW_BYTES="$(zcat "$TARBALL" | wc -c)"
IMG_MIB=$(( (RAW_BYTES * 2 + 1024 * 1024 - 1) / (1024 * 1024) ))
[ "$IMG_MIB" -lt 64 ] && IMG_MIB=64

echo "==> rootfs ~$((RAW_BYTES / 1024 / 1024)) MiB uncompressed, image ${IMG_MIB} MiB (2x, 64 floor)"

#-----------------------------------------------------------------------------
# Allocate, format, mount, populate, unmount
#-----------------------------------------------------------------------------

IMG="$WORK_DIR/alpine-rootfs.img"
MNT="$WORK_DIR/mnt"
LOOP_DEV=""

cleanup() {
    if mountpoint -q "$MNT" 2>/dev/null; then
        sudo umount "$MNT" || true
    fi
    if [ -n "$LOOP_DEV" ] && [ -e "$LOOP_DEV" ]; then
        sudo losetup -d "$LOOP_DEV" 2>/dev/null || true
    fi
}
trap cleanup EXIT

rm -f "$IMG"
mkdir -p "$MNT"
truncate -s "${IMG_MIB}M" "$IMG"

echo "==> mkfs.ext4 -L yetty-root -E resize=4G $IMG"
mkfs.ext4 -F -L yetty-root -E resize=4G "$IMG" >/dev/null

LOOP_DEV="$(sudo losetup -f --show "$IMG")"
echo "==> loop device: $LOOP_DEV"
sudo mount "$LOOP_DEV" "$MNT"

echo "==> extracting rootfs into image"
sudo tar -C "$MNT" -xzpf "$TARBALL"

# /init for tinyemu console boot — matches linux asset's slirp user-mode net.
sudo tee "$MNT/init" >/dev/null << 'INIT_EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null || true
hostname tinyemu

ip link set lo up
ip link set eth0 up 2>/dev/null
ip addr add 10.0.2.15/24 dev eth0 2>/dev/null
ip route add default via 10.0.2.2 2>/dev/null
echo 'nameserver 10.0.2.3' > /etc/resolv.conf

# yetty-tools-riscv: yetty attaches a second virtio-blk drive (drive1) at
# /dev/vdb carrying riscv64 demos+tools and a repo HEAD checkout. Mount RO
# at /opt/yetty when present. Skipped silently otherwise so this image
# still boots in plain tinyemu invocations that don't pass a second disk.
if [ -b /dev/vdb ]; then
    mkdir -p /opt/yetty
    mount -o ro /dev/vdb /opt/yetty 2>/dev/null
fi

exec /bin/sh
INIT_EOF
sudo chmod 755 "$MNT/init"
sudo chown 0:0 "$MNT/init"

sync
sudo umount "$MNT"
sudo losetup -d "$LOOP_DEV"
LOOP_DEV=""

#-----------------------------------------------------------------------------
# Stage + package
#-----------------------------------------------------------------------------

STAGE="$WORK_DIR/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"

# Brotli q11 the rootfs image — this is the biggest single asset; raw
# ext4 is highly compressible (~5-10x) so the binary-size saving is
# substantial. Embed pipeline picks the .br up pre-compressed; runtime
# path mode gets a decompressed copy at consumer-side fetch time
# (3rdparty-fetch.cmake).
: "${BROTLI_QUALITY:=11}"
echo "==> brotli alpine-rootfs.img (quality $BROTLI_QUALITY)"
in_size="$(stat -c%s "$IMG" 2>/dev/null || stat -f%z "$IMG")"
brotli -q "$BROTLI_QUALITY" -f -o "$STAGE/alpine-rootfs.img.br" "$IMG"
out_size="$(stat -c%s "$STAGE/alpine-rootfs.img.br" 2>/dev/null || stat -f%z "$STAGE/alpine-rootfs.img.br")"
printf "    alpine-rootfs.img  %10d -> %10d bytes (%.1f%%)\n" \
    "$in_size" "$out_size" \
    "$(awk -v a="$out_size" -v b="$in_size" 'BEGIN{printf "%.1f", a/b*100}')"

OUT_TARBALL="$OUTPUT_DIR/alpine-disk-${VERSION}.tar.gz"
echo "==> packaging -> $OUT_TARBALL"
tar -C "$STAGE" -czf "$OUT_TARBALL" .

echo ""
echo "Alpine disk asset ready:"
ls -lh "$OUT_TARBALL"
tar -tzf "$OUT_TARBALL"
