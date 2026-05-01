#!/usr/bin/env bash
# Build and run the original tinyemu (2019-12-21) from bellard.org with the
# jslinux buildroot riscv64 image. Used to compare slirp/network behaviour
# against our forked tinyemu under src/tinyemu/.
set -euo pipefail

WORKDIR="$PWD/temu"
TARBALL_URL="https://bellard.org/tinyemu/tinyemu-2019-12-21.tar.gz"
SRC_DIR="$WORKDIR/tinyemu-2019-12-21"

KERNEL_URL="https://bellard.org/jslinux/kernel-riscv64.bin"
BBL_URL="https://bellard.org/jslinux/bbl64.bin"
CFG_URL="https://bellard.org/jslinux/buildroot-riscv64.cfg"

mkdir -p "$WORKDIR"
cd "$WORKDIR"

# 1. Source
if [ ! -d "$SRC_DIR" ]; then
    echo "[orig-temu] downloading $TARBALL_URL"
    curl -sSfLO "$TARBALL_URL"
    tar xzf "tinyemu-2019-12-21.tar.gz"
fi

# 2. Build (system gcc/headers; avoid nix toolchain that lacks /usr/include)
if [ ! -x "$SRC_DIR/temu" ]; then
    echo "[orig-temu] building with PATH=/usr/bin make"
    ( cd "$SRC_DIR" && PATH=/usr/bin make )
fi

# 3. Assets (kept next to the source so paths in the cfg stay relative)
cd "$SRC_DIR"
for url in "$KERNEL_URL" "$BBL_URL" "$CFG_URL"; do
    f="$(basename "$url")"
    if [ ! -f "$f" ]; then
        echo "[orig-temu] downloading $f"
        curl -sSfLO "$url"
    fi
done

# 4. Run
echo "[orig-temu] launching ./temu buildroot-riscv64.cfg"
exec ./temu buildroot-riscv64.cfg "$@"
