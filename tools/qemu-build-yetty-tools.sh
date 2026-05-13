#!/usr/bin/env bash
# Iteration-friendly clone of ./tools/qemu.sh for figuring out, by hand,
# what it actually takes to build the yetty riscv64 tools inside an
# alpine-extended VM. Boots the same kernel+rootfs as qemu.sh, drops the
# yetty-tools-riscv side-disk (we're rebuilding it from source here),
# and adds a virtio-9p export of the host's repo root.
#
# Inside the guest, after telnet login (host: telnet 127.0.0.1 2423):
#
#   sudo -i
#   mkdir -p /mnt/yetty-src
#   mount -t 9p -o trans=virtio,version=9p2000.L yettysrc /mnt/yetty-src
#   apk add --no-cache cmake samurai gcc binutils make musl-dev \
#                      linux-headers pkgconf git
#   cd /mnt/yetty-src
#   cmake -S . -B /root/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DYETTY_ENABLE_LIB_WEBGPU=OFF \
#         -DYETTY_ENABLE_LIB_GLFW=OFF \
#         -DYETTY_ENABLE_LIB_QEMU=OFF \
#         -DYETTY_ENABLE_LIB_QEMU_BINARY=OFF
#   cmake --build /root/build --parallel
#
# Build out-of-tree under /root/build so the 9p-shared source stays
# pristine on the host.
#
# Usage: ./tools/qemu-build-yetty-tools.sh [<extra qemu args>...]

set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

export YETTY_RUNTIME_DIR="$HOME/.local/share/yetty"
QEMU="$YETTY_RUNTIME_DIR/qemu/qemu-system-riscv64"

required=(
    "$YETTY_RUNTIME_DIR/yemu/opensbi-fw_dynamic.bin"
    "$YETTY_RUNTIME_DIR/yemu/kernel-riscv64.bin"
    "$YETTY_RUNTIME_DIR/yemu/alpine-extended-rootfs.img"
    "$QEMU"
)
for f in "${required[@]}"; do
    [[ -e "$f" ]] || { echo "qemu-build-yetty-tools.sh: missing $f" >&2; exit 1; }
done

# -smp 4 / -m 2G: in-VM cmake + ninja need real CPU and RAM. qemu.sh uses
# -smp 1 -m 256 because the runtime use case is just running prebuilt
# binaries; a from-source build under TCG with those numbers crawls.
#
# 9p: security_model=passthrough preserves host uids — qemu runs under
# sudo, so the guest sees files with their real ownership. The yetty user
# in the guest is uid 1000, which usually matches the developer's uid on
# the host, so files appear owned by yetty inside.
#
# No drive1 / yetty-riscv-disk.img — we're rebuilding what would have
# gone into that disk.
exec sudo "$QEMU" \
    -machine virt \
    -smp 4 \
    -m 2G \
    -bios "$YETTY_RUNTIME_DIR/yemu/opensbi-fw_dynamic.bin" \
    -kernel "$YETTY_RUNTIME_DIR/yemu/kernel-riscv64.bin" \
    -append "earlycon=sbi console=hvc0 root=/dev/vda rw init=/init" \
    -drive "file=$YETTY_RUNTIME_DIR/yemu/alpine-extended-rootfs.img,if=none,format=raw,id=hd0" \
    -device virtio-blk-device,drive=hd0 \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2423-:23 \
    -device virtio-net-device,netdev=net0 \
    -fsdev "local,id=yettysrc,path=$REPO_ROOT,security_model=passthrough,multidevs=remap" \
    -device virtio-9p-device,fsdev=yettysrc,mount_tag=yettysrc \
    -device virtio-serial-device \
    -device virtconsole,chardev=char0 \
    -chardev stdio,id=char0,signal=off \
    -serial none \
    -display none \
    -no-reboot \
    -no-shutdown \
    "$@"
