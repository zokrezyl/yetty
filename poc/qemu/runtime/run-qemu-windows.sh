#!/usr/bin/env bash
# Run qemu-system-riscv64.exe (built by poc/qemu/build-tools/build-windows-minimal.sh).
#
# Default: boot OpenSBI on a virt machine with slirp networking, serial console
# on stdio. You will see the OpenSBI banner immediately. Ctrl-A then C toggles
# to the qemu monitor; Ctrl-A then X exits.
#
# To boot an actual guest kernel:
#   KERNEL=/path/to/kernel-riscv64.bin ./run-qemu.sh
#   KERNEL=... ROOTFS_IMG=/path/to/rootfs.img ./run-qemu.sh
#
# BUILD_DIR=... overrides the default build-windows-minimal/.

set -e

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-windows-minimal}"
QEMU="$BUILD_DIR/qemu-system-riscv64.exe"
PC_BIOS="${PC_BIOS:-$REPO_ROOT/poc/qemu/build-tools/qemu-11.0.0-rc4/pc-bios}"

# Auto-locate kernel + rootfs from yetty's installed assets if KERNEL/ROOTFS
# not provided. Fall back gracefully if the bundle is missing.
YETTY_ASSETS="${YETTY_ASSETS:-$LOCALAPPDATA/yetty/data/yemu}"
if [ -z "$LOCALAPPDATA" ] && [ -d "$USERPROFILE/AppData/Local/yetty/data/yemu" ]; then
    YETTY_ASSETS="$USERPROFILE/AppData/Local/yetty/data/yemu"
fi
: "${KERNEL:=$YETTY_ASSETS/kernel-riscv64.bin}"
: "${ROOTFS_IMG:=$YETTY_ASSETS/alpine-rootfs.img}"

if [ ! -x "$QEMU" ]; then
    echo "ERROR: $QEMU not found." >&2
    echo "Run poc/qemu/build-tools/build-windows-minimal.sh first." >&2
    exit 1
fi
if [ ! -d "$PC_BIOS" ]; then
    echo "ERROR: pc-bios dir not found at $PC_BIOS" >&2
    exit 1
fi

args=(
    -L "$PC_BIOS"
    -machine virt
    -smp 1
    -m 256
    -bios default
    -netdev "user,id=n0,hostfwd=tcp::2222-:22"
    -device virtio-net-device,netdev=n0
    # Console wiring matches yetty's kernel/init expectations: hvc0 over
    # virtio-console with stdio as the chardev. Monitor is multiplexed so
    # Ctrl-A C still works.
    -device virtio-serial-device
    -device virtconsole,chardev=char0
    -chardev stdio,id=char0,signal=off,mux=on
    -mon chardev=char0,mode=readline
    -serial none
    -display none
)

if [ -f "$KERNEL" ]; then
    args+=(-kernel "$KERNEL")
    if [ -f "$ROOTFS_IMG" ]; then
        # virtio-blk root: this build is --without-default-features so 9p is
        # off; using a raw block image keeps the qemu config minimal.
        args+=(
            -append "earlycon=sbi console=hvc0 root=/dev/vda rw init=/init"
            -drive "file=$ROOTFS_IMG,format=raw,if=none,id=hd0"
            -device virtio-blk-device,drive=hd0
        )
    else
        args+=(-append "earlycon=sbi console=hvc0")
    fi
else
    echo "WARNING: no kernel at $KERNEL -- OpenSBI will boot but freeze (no payload)." >&2
    echo "         Set KERNEL=... and (optional) ROOTFS_IMG=... to actually boot." >&2
fi

cat <<MSG
qemu: $QEMU
Ctrl-A C  -> qemu monitor (try: info usernet)
Ctrl-A X  -> exit
host:2222 -> guest:22 (slirp port-forward)
---
MSG

exec "$QEMU" "${args[@]}"
