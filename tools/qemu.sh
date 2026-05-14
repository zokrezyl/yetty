#!/usr/bin/env bash
# Standalone launcher for the bundled qemu-system-riscv64 binary.
#
# Sets YETTY_RUNTIME_DIR (referenced by assets/yemu/qemu/qemu-qemu.ini
# via $YETTY_RUNTIME_DIR), checks the assets the ini expects are
# present, then runs qemu with that argv.
#
# Usage: ./tools/qemu.sh [<extra qemu args>...]

set -euo pipefail

cd "$(dirname "$0")/.."

export YETTY_RUNTIME_DIR="$HOME/.local/share/yetty"

INI="assets/yemu/qemu/qemu-qemu.ini"
QEMU="$YETTY_RUNTIME_DIR/qemu/qemu-system-riscv64"

required=(
    "$YETTY_RUNTIME_DIR/yemu/opensbi-fw_dynamic.bin"
    "$YETTY_RUNTIME_DIR/yemu/kernel-riscv64.bin"
    "$YETTY_RUNTIME_DIR/yemu/yetty-rootfs-riscv.img"
    "$QEMU"
)
for f in "${required[@]}"; do
    [[ -e "$f" ]] || { echo "qemu.sh: missing $f" >&2; exit 1; }
done

# Strip comments/blank lines, expand $YETTY_* env vars, parse with shell
# word-splitting so quoted args (e.g. -append "..." for the kernel
# cmdline) stay as a single argv entry.
content="$(sed -e 's/#.*$//' "$INI")"
eval "args=( $content )"

sudo "$QEMU" "${args[@]}" "$@"
