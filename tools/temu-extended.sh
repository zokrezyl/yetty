#!/usr/bin/env bash
# Standalone launcher for the bundled temu binary (extended rootfs).
#
# Same as ./tools/temu.sh but loads temu-temu-extended.cfg, which boots the
# 2 GiB alpine-extended rootfs (Alpine + dev tools) instead of the small
# minirootfs.
#
# Sets YETTY_RUNTIME_DIR (referenced by assets/yemu/temu/temu-temu-extended.cfg
# via $YETTY_RUNTIME_DIR), checks that the assets the cfg expects are
# present, then runs temu.
#
# Usage: ./tools/temu-extended.sh [<extra temu args>...]

set -euo pipefail

cd "$(dirname "$0")/.."

export YETTY_RUNTIME_DIR="$HOME/.local/share/yetty"

CFG="assets/yemu/temu/temu-temu-extended.cfg"
TEMU="$PWD/build-desktop-ytrace-release/temu"

required=(
    "$YETTY_RUNTIME_DIR/yemu/opensbi-fw_jump.elf"
    "$YETTY_RUNTIME_DIR/yemu/kernel-riscv64.bin"
    "$YETTY_RUNTIME_DIR/yemu/alpine-extended-rootfs.img"
)
for f in "${required[@]}"; do
    [[ -e "$f" ]] || { echo "temu-extended.sh: missing $f (run cmake --build build-desktop-ytrace-release)" >&2; exit 1; }
done
[[ -x "$TEMU" ]] || { echo "temu-extended.sh: missing $TEMU" >&2; exit 1; }

exec "$TEMU" "$CFG" "$@"
