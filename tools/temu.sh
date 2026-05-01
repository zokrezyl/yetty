#!/usr/bin/env bash
# Standalone launcher for the bundled temu binary.
#
# Sets YETTY_RUNTIME_DIR (referenced by assets/yemu/temu/temu-temu.cfg
# via $YETTY_RUNTIME_DIR), checks that the assets the cfg expects are
# present, then runs temu.
#
# Usage: ./tools/temu.sh [<extra temu args>...]

set -euo pipefail

cd "$(dirname "$0")/.."

export YETTY_RUNTIME_DIR="$HOME/.local/share/yetty"

CFG="assets/yemu/temu/temu-temu.cfg"
TEMU="$PWD/build-desktop-ytrace-release/temu"

required=(
    "$YETTY_RUNTIME_DIR/yemu/opensbi-fw_jump.elf"
    "$YETTY_RUNTIME_DIR/yemu/kernel-riscv64.bin"
    "$YETTY_RUNTIME_DIR/yemu/alpine-rootfs.img"
)
for f in "${required[@]}"; do
    [[ -e "$f" ]] || { echo "temu.sh: missing $f (run cmake --build build-desktop-ytrace-release)" >&2; exit 1; }
done
[[ -x "$TEMU" ]] || { echo "temu.sh: missing $TEMU" >&2; exit 1; }

exec "$TEMU" "$CFG" "$@"
