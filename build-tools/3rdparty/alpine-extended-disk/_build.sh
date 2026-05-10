#!/bin/bash
# Build a raw ext4 disk image from an Alpine minirootfs *plus* the JSLinux
# package set, by booting the freshly-laid-down rootfs in qemu-system-riscv64
# and letting Alpine's own apk install the packages from inside the VM.
# Produces alpine-extended-disk-${VERSION}.tar.gz containing
# alpine-extended-rootfs.img.br.
#
# Why an in-VM install (and not `apk --root --arch riscv64` from the host):
# scriptlets are run by the target's own /sbin/apk against the target libc,
# which is exactly what a real Alpine install does. The host stays clean and
# we don't depend on qemu-user / binfmt.
#
# The kernel that boots the install VM comes from this repo's `linux` 3rdparty
# producer (already configured for tinyemu/qemu-system-riscv64 -machine virt:
# virtio-mmio, virtio-blk, virtio-net, ext4). We fetch the prebuilt tarball
# from this repo's GitHub releases (lib-linux-<ver>) — same convention as
# other inter-3rdparty deps (see libpng/_build.sh).
#
# Env vars:
#   VERSION           derived — read from ./version (used in output filename)
#   OUTPUT_DIR        required — where to place the tarball
#   WORK_DIR          optional — intermediate build tree
#                                (default: /tmp/yetty-asset-alpine-extended-disk)
#   CACHE_DIR         optional — download cache for fetched tarballs
#                                (default: $HOME/.cache/yetty-3rdparty)
#   IMAGE_MIB         optional — initial raw disk size
#                                (default: 2048; sized big to fit the package
#                                set + apk metadata + headroom)
#   QEMU_TIMEOUT_SEC  optional — hard timeout on the in-VM install
#                                (default: 1800; protects CI)
#   YETTY_3RDPARTY_URL_BASE  optional — release URL prefix for the linux dep
#                                       (default: https://github.com/zokrezyl/yetty/releases/download)
#
# The `version` file format is <alpine-release>-<arch>-<pkg-rev>, e.g.
# `3.23.4-riscv64-1`. Same scheme as alpine-disk; see that producer for
# the rationale. The kernel for the install VM is fetched from the
# `linux` 3rdparty release at whatever <pkg-version> is pinned in
# build-tools/3rdparty/linux/version (full <upstream>-<rev> string).
#
# Needs: curl, brotli, e2fsprogs (mkfs.ext4), util-linux (losetup, mount),
# tar, gzip, qemu-system-riscv64, and passwordless sudo (for losetup/mount/
# umount). The `assets-alpine-extended` nix shell wires all of these up.

set -euo pipefail

#-----------------------------------------------------------------------------
# Versioning + paths
#-----------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

# Split <alpine-release>-<arch>-<pkg-rev>. Same scheme as alpine-disk.
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
ALPINE_VERSION="${ALPINE_RELEASE%.*}"

# Pinned kernel version is the *whole* version string from linux/version
# (e.g. `7.0-1`) — that's the lib-linux-* tag suffix and the tarball
# infix simultaneously, so we use it verbatim in the URL.
LINUX_VERSION_FILE="$REPO_ROOT/build-tools/3rdparty/linux/version"
[ -f "$LINUX_VERSION_FILE" ] || { echo "missing $LINUX_VERSION_FILE" >&2; exit 1; }
LINUX_PKG_VERSION="$(tr -d '[:space:]' < "$LINUX_VERSION_FILE")"
[ -n "$LINUX_PKG_VERSION" ] || { echo "$LINUX_VERSION_FILE is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-asset-alpine-extended-disk}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
IMAGE_MIB="${IMAGE_MIB:-300}"
QEMU_TIMEOUT_SEC="${QEMU_TIMEOUT_SEC:-1800}"
URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"

ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION}/releases/${ALPINE_ARCH}/alpine-minirootfs-${ALPINE_RELEASE}-${ALPINE_ARCH}.tar.gz"
LINUX_TAR_URL="$URL_BASE/lib-linux-${LINUX_PKG_VERSION}/linux-${LINUX_PKG_VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"
cd "$WORK_DIR"

#-----------------------------------------------------------------------------
# Fetch deps: minirootfs + prebuilt RISC-V kernel
#-----------------------------------------------------------------------------

# flock-guarded fetch so concurrent invocations don't race the same cache slot.
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

ALPINE_TARBALL="$CACHE_DIR/alpine-minirootfs-${ALPINE_RELEASE}-${ALPINE_ARCH}.tar.gz"
LINUX_TARBALL="$CACHE_DIR/linux-${LINUX_PKG_VERSION}.tar.gz"

fetch "$ALPINE_URL"   "$ALPINE_TARBALL" "Alpine ${ALPINE_RELEASE} minirootfs (${ALPINE_ARCH})" alpine-extended-minirootfs
fetch "$LINUX_TAR_URL" "$LINUX_TARBALL"  "linux ${LINUX_PKG_VERSION} (build-time dep, kernel for the install VM)" alpine-extended-linux

# Extract the kernel image. linux-*.tar.gz is created with `tar -C STAGE .`,
# so its members are stored with a `./` prefix (e.g. `./kernel-riscv64.bin.br`).
# Asking tar for the bare name without `./` doesn't match, so we just unpack
# the whole tarball — it's only ~6 MB, the cost is irrelevant — and read the
# kernel from the extracted tree. Then brotli-decompress to a raw Image
# suitable for `qemu -kernel`.
LINUX_EXTRACT="$WORK_DIR/linux-extract"
rm -rf "$LINUX_EXTRACT"
mkdir -p "$LINUX_EXTRACT"
tar -C "$LINUX_EXTRACT" -xzf "$LINUX_TARBALL"
KERNEL_BR="$LINUX_EXTRACT/kernel-riscv64.bin.br"
[ -f "$KERNEL_BR" ] || { echo "missing $KERNEL_BR after extracting $LINUX_TARBALL" >&2; exit 1; }
KERNEL_BIN="$WORK_DIR/kernel-riscv64.bin"
brotli -d -f -o "$KERNEL_BIN" "$KERNEL_BR"

#-----------------------------------------------------------------------------
# Allocate, format, mount, populate
#-----------------------------------------------------------------------------

IMG="$WORK_DIR/alpine-extended-rootfs.img"
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
truncate -s "${IMAGE_MIB}M" "$IMG"

echo "==> mkfs.ext4 -L yetty-root-ext -E resize=4G ${IMAGE_MIB} MiB image"
# resize=4G reserves GDT slots so a later online resize2fs up to 4 GiB stays
# online (matches alpine-disk's choice).
mkfs.ext4 -F -L yetty-root-ext -E resize=4G "$IMG" >/dev/null

LOOP_DEV="$(sudo losetup -f --show "$IMG")"
echo "==> loop device: $LOOP_DEV"
sudo mount "$LOOP_DEV" "$MNT"

echo "==> extracting Alpine minirootfs into image"
sudo tar -C "$MNT" -xzpf "$ALPINE_TARBALL"

# Apply the fs/ overlay BEFORE the install VM boots. This lays down
# everything the install needs:
#   /init                                  — runtime PID 1 (used post-install)
#   /install.sh                            — one-shot init for the install VM
#   /etc/sudoers.d/wheel-nopasswd          — referenced by install.sh / sudo pkg
#   /etc/service/telnetd/run               — runit service supervised by /init
#   /etc/profile.d/yetty.sh                — TERM/TERM_PROGRAM defaults
#   /home/yetty/.bashrc                    — user shell defaults (DROPPED on
#                                            this pass; adduser inside the VM
#                                            won't see /home/yetty as already
#                                            existing-and-root-owned, which
#                                            confuses skel-copy logic; the
#                                            second pass after the VM lays it
#                                            down on top of the freshly
#                                            adduser'd home dir.)
#   /usr/local/sbin/yetty-telnetd-login    — telnetd wrapper that auto-logins
#                                            as yetty
#
# Strategy: tar-pipe the overlay into MNT, excluding /home (and also
# excluding /install.sh on the *post-VM* pass — see below). tar is in the
# nix shell; rsync isn't.
FS_OVERLAY="$SCRIPT_DIR/fs"
[ -d "$FS_OVERLAY" ] || { echo "missing $FS_OVERLAY" >&2; exit 1; }

apply_fs_overlay() {
    # $1 = list of --exclude args (space-separated). cp -a doesn't have
    # --exclude; tar pipe does. Preserves mode (incl. +x), timestamps,
    # symlinks; maps owner to root. set -eo pipefail inside sudo bash -c
    # so a failing source-tar (missing dir, broken read) trips the build
    # instead of silently producing an empty image overlay.
    local excludes="$1"
    sudo bash -c "
        set -eo pipefail
        cd '$FS_OVERLAY'
        tar $excludes --owner=0 --group=0 -cf - . | tar -C '$MNT' -xpf -
    "
}

echo "==> applying fs overlay (pre-install, sans /home/yetty)"
apply_fs_overlay "--exclude=./home/yetty"
sudo chmod 755 "$MNT/init" "$MNT/install.sh"
sudo chmod 0440 "$MNT/etc/sudoers.d/wheel-nopasswd"
sudo chmod +x "$MNT/etc/service/telnetd/run" "$MNT/usr/local/sbin/yetty-telnet-login"

sync
sudo umount "$MNT"
sudo losetup -d "$LOOP_DEV"
LOOP_DEV=""

#-----------------------------------------------------------------------------
# Boot the disk in qemu-system-riscv64 and let it run /install.sh
#-----------------------------------------------------------------------------
#
# Console plumbing: this kernel (yetty's `linux` 3rdparty, configured for
# tinyemu) has CONFIG_VIRTIO_CONSOLE=y but no 8250 UART driver. So we wire
# up a virtio-serial-device + virtconsole as hvc0 and pass console=hvc0 on
# the kernel cmdline. earlycon=sbi gives us boot output before HVC is up.
#
# Network: -netdev user is qemu's slirp (10.0.2.0/24, dns 10.0.2.3, gw
# 10.0.2.2) — same address plan as install.sh expects.
#
# -no-reboot: poweroff exits qemu instead of bouncing back into firmware.

QEMU_LOG="$WORK_DIR/qemu-install.log"
echo "==> booting install VM (qemu-system-riscv64, log: $QEMU_LOG)"

# `timeout` returns 124 on hard kill; treat that as failure with a clear
# message. SIGTERM first, then SIGKILL after 10s grace.
set +e
timeout --kill-after=10s "${QEMU_TIMEOUT_SEC}s" \
    qemu-system-riscv64 \
        -machine virt -m 1G -smp 4 \
        -bios default \
        -kernel "$KERNEL_BIN" \
        -append "console=hvc0 earlycon=sbi root=/dev/vda rw init=/install.sh" \
        -drive "file=$IMG,format=raw,if=none,id=hd0" \
        -device virtio-blk-device,drive=hd0 \
        -netdev user,id=net0 \
        -device virtio-net-device,netdev=net0 \
        -chardev stdio,id=ch0,signal=off,mux=on \
        -device virtio-serial-device \
        -device virtconsole,chardev=ch0 \
        -display none -no-reboot \
        </dev/null >"$QEMU_LOG" 2>&1
QEMU_RC=$?
set -e

echo "==> qemu exited rc=$QEMU_RC"
if [ "$QEMU_RC" -eq 124 ] || [ "$QEMU_RC" -eq 137 ]; then
    echo "FAILED: install VM hit ${QEMU_TIMEOUT_SEC}s timeout — see $QEMU_LOG" >&2
    tail -n 80 "$QEMU_LOG" >&2 || true
    exit 1
fi
if [ "$QEMU_RC" -ne 0 ]; then
    echo "FAILED: qemu exited with rc=$QEMU_RC — see $QEMU_LOG" >&2
    tail -n 80 "$QEMU_LOG" >&2 || true
    exit 1
fi

# Sanity check: install.sh must have erased itself; if it's still there, the
# install loop ran out of disk / network or apk failed before reaching the
# `rm -f /install.sh` line.
LOOP_DEV="$(sudo losetup -f --show "$IMG")"
sudo mount "$LOOP_DEV" "$MNT"
if [ -e "$MNT/install.sh" ]; then
    echo "FAILED: /install.sh still present in image — install did not reach completion" >&2
    echo "        last 80 lines of qemu log:" >&2
    tail -n 80 "$QEMU_LOG" >&2 || true
    exit 1
fi
# Useful one-liner so the build log shows what landed.
INSTALLED_COUNT="$(sudo find "$MNT" -path "$MNT/proc" -prune -o -path "$MNT/sys" -prune -o -type f -print 2>/dev/null | wc -l)"
echo "==> install VM completed: ${INSTALLED_COUNT} files in image"

# Re-apply the fs/ overlay AFTER the install VM, so anything apk
# clobbered (or that adduser stamped fresh from /etc/skel into
# /home/yetty/) is overwritten by the repo-committed truth. This pass
# excludes /install.sh — the VM self-deleted it, and we don't want to
# re-stage it into the shipped image.
echo "==> applying fs overlay (post-install, sans /install.sh)"
apply_fs_overlay "--exclude=./install.sh"
# Re-assert the executable + 0440 bits the tar pipe carries from the
# repo's own modes (in case the host repo lost the +x somewhere; cheap
# to do unconditionally).
sudo chmod 755 "$MNT/init"
sudo chmod 0440 "$MNT/etc/sudoers.d/wheel-nopasswd"
sudo chmod +x "$MNT/etc/service/telnetd/run" "$MNT/usr/local/sbin/yetty-telnet-login"
# Per-user dirs need correct ownership — adduser created /home/yetty
# owned by uid/gid 1000, but our overlay just stomped on it as root.
if [ -d "$MNT/home/yetty" ]; then
    sudo chown -R 1000:1000 "$MNT/home/yetty"
fi

sudo umount "$MNT"
sudo losetup -d "$LOOP_DEV"
LOOP_DEV=""

#-----------------------------------------------------------------------------
# Stage + package
#-----------------------------------------------------------------------------

STAGE="$WORK_DIR/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"

# Brotli q11 — same rationale as alpine-disk: the embed pipeline picks the
# .br up pre-compressed, runtime path mode gets a decompressed copy
# alongside via 3rdparty-fetch.cmake. Raw ext4 with mostly-zeroed unused
# blocks compresses very well even at this size.
: "${BROTLI_QUALITY:=6}"
echo "==> brotli alpine-extended-rootfs.img (quality $BROTLI_QUALITY)"
in_size="$(stat -c%s "$IMG" 2>/dev/null || stat -f%z "$IMG")"
brotli -q "$BROTLI_QUALITY" -f -o "$STAGE/alpine-extended-rootfs.img.br" "$IMG"
out_size="$(stat -c%s "$STAGE/alpine-extended-rootfs.img.br" 2>/dev/null || stat -f%z "$STAGE/alpine-extended-rootfs.img.br")"
printf "    alpine-extended-rootfs.img  %12d -> %12d bytes (%.1f%%)\n" \
    "$in_size" "$out_size" \
    "$(awk -v a="$out_size" -v b="$in_size" 'BEGIN{printf "%.1f", a/b*100}')"

OUT_TARBALL="$OUTPUT_DIR/alpine-extended-disk-${VERSION}.tar.gz"
echo "==> packaging -> $OUT_TARBALL"
tar -C "$STAGE" -czf "$OUT_TARBALL" .

echo ""
echo "Alpine extended disk asset ready:"
ls -lh "$OUT_TARBALL"
tar -tzf "$OUT_TARBALL"
