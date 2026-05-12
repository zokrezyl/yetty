#!/usr/bin/env bash
# Builds a minimum-size ext4 disk image containing the riscv64 binaries
# produced by `make build-linux-riscv-ytrace-release` plus a clean
# checkout of the repo at HEAD. Designed to be attached as a virtio-blk
# drive inside temu/qemu so the emulated guest has both the executables
# (under /yetty/bin) and the source tree (under /yetty/repo) available.
#
# Uses `mke2fs -d <root>` so no losetup/sudo is required, then shrinks
# the image with `resize2fs -M` to the smallest size that holds the
# data — important because the asset is committed and shipped.
#
# Inputs:  $REPO_ROOT/build-linux-riscv-ytrace-release/
# Output:  $REPO_ROOT/build-linux-riscv-ytrace-release/yetty-riscv-disk.img

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

REPO_ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-linux-riscv-ytrace-release}"
OUT_IMG="${OUT_IMG:-$BUILD_DIR/yetty-riscv-disk.img}"

# Binaries to ship. Keep in sync with LINUX_RISCV_TARGETS in Makefile.
BINARIES=(
    "$BUILD_DIR/tools/decode-ypaint/decode-ypaint"
    "$BUILD_DIR/tools/msdf/cdb-viewer/cdb-viewer"
    "$BUILD_DIR/tools/msdf/cdb-diff/cdb-diff"
    "$BUILD_DIR/tools/msdf/gen-msdf/yetty-msdf-gen"
    "$BUILD_DIR/src/yetty/ymsdf-gen/yetty-ymsdf-gen"
    "$BUILD_DIR/temu"
    "$BUILD_DIR/demo/ymgui/01_demo_window/demo-ymgui-01-demo-window"
    # Client tools unblocked by issue #132 (yplot/yimage/ymesh/ypaint/yfont
    # splits) — emit OSC complex-prim envelopes from a riscv64 guest, no GPU.
    "$BUILD_DIR/tools/yplot/yplot"
    "$BUILD_DIR/tools/ymesh/ymesh"
    "$BUILD_DIR/tools/yecho/yecho"
    "$BUILD_DIR/tools/ycat/ycat"
    "$BUILD_DIR/tools/ygreeter/ygreeter"
)

for b in "${BINARIES[@]}"; do
    [ -f "$b" ] || { echo "missing binary: $b — run 'make build-linux-riscv-ytrace-release' first" >&2; exit 1; }
done

for tool in truncate mke2fs e2fsck resize2fs dumpe2fs git tar brotli; do
    command -v "$tool" >/dev/null || { echo "required tool not found: $tool" >&2; exit 1; }
done

STAGE_DIR="$(mktemp -d -t yetty-riscv-disk-XXXXXX)"
trap 'rm -rf "$STAGE_DIR"' EXIT

#-----------------------------------------------------------------------------
# Stage: /yetty/bin (built executables) + /yetty/repo (clean HEAD checkout)
#-----------------------------------------------------------------------------

mkdir -p "$STAGE_DIR/yetty/bin" "$STAGE_DIR/yetty/repo"

echo "==> staging riscv64 binaries"
for b in "${BINARIES[@]}"; do
    cp "$b" "$STAGE_DIR/yetty/bin/"
done

echo "==> staging git checkout (HEAD)"
git -C "$REPO_ROOT" archive HEAD | tar -x -C "$STAGE_DIR/yetty/repo"

# Drop heavy already-compressed assets that brotli can't shrink. The
# guest doesn't need them for the riscv build/demo path; keep them out
# of the shipped image to roughly halve the .br size.
#   docs/pres.gif       — 21 MB LZW-compressed slideshow
#   assets/fonts/*.ttf  — 10 MB DejaVu Nerd Fonts (font atlases live in
#                         the CDB; raw TTFs are only used by the
#                         desktop yetty exec, not by anything we build
#                         for riscv64)
rm -f "$STAGE_DIR/yetty/repo/docs/pres.gif"
rm -f "$STAGE_DIR/yetty/repo/assets/fonts/"*.ttf

#-----------------------------------------------------------------------------
# Allocate a generously-sized image, format with mke2fs -d (no sudo needed),
# then shrink to the minimum with resize2fs -M and truncate the file to match.
#-----------------------------------------------------------------------------

RAW_BYTES="$(du -sb "$STAGE_DIR" | awk '{print $1}')"
# Headroom for ext4 metadata (inode table, journal, group descriptors).
# 2x is the same factor alpine-disk uses; floor at 64 MiB so very small
# stagings still leave room for the journal.
ALLOC_BYTES=$(( RAW_BYTES * 2 ))
ALLOC_MIB=$(( (ALLOC_BYTES + 1024 * 1024 - 1) / (1024 * 1024) ))
[ "$ALLOC_MIB" -lt 64 ] && ALLOC_MIB=64

echo "==> staged $((RAW_BYTES / 1024 / 1024)) MiB; allocating ${ALLOC_MIB} MiB ext4 image"

rm -f "$OUT_IMG"
truncate -s "${ALLOC_MIB}M" "$OUT_IMG"

# -O ^has_journal saves a few MiB on tiny images and the guest is a
# read-mostly demo target — but keep the journal: alpine-disk does, and
# guest write workloads (e.g. extracting build artefacts inside the VM)
# benefit from it. Trade-off is small at these sizes.
mke2fs -t ext4 -F -L yetty-riscv -d "$STAGE_DIR" "$OUT_IMG" >/dev/null

# Shrink to minimum. e2fsck must run clean before resize2fs -M.
e2fsck -fy "$OUT_IMG" >/dev/null
resize2fs -M "$OUT_IMG" 2>&1 | tail -1

# Truncate the host file to the new filesystem size (resize2fs only
# updates the superblock — the file on disk stays at ALLOC_MIB).
BLOCK_COUNT="$(dumpe2fs -h "$OUT_IMG" 2>/dev/null | awk -F: '/Block count/{gsub(/ /,"",$2); print $2}')"
BLOCK_SIZE="$(dumpe2fs -h "$OUT_IMG"  2>/dev/null | awk -F: '/Block size/{gsub(/ /,"",$2);  print $2}')"
NEW_BYTES=$(( BLOCK_COUNT * BLOCK_SIZE ))
truncate -s "$NEW_BYTES" "$OUT_IMG"

#-----------------------------------------------------------------------------
# Brotli-compress the image at quality 6 — same scheme alpine-disk uses
# (it ships .br alongside the raw .img, decompressed on consume). q6 is
# the fast/medium tradeoff: ext4 is highly compressible (~5–10x) and q11
# would dominate the build, while q6 keeps build time low and still gets
# most of the ratio.
#-----------------------------------------------------------------------------
echo "==> brotli compress (quality 6)"
brotli -q 6 -f -o "$OUT_IMG.br" "$OUT_IMG"

echo
echo "yetty riscv disk ready:"
ls -lh "$OUT_IMG" "$OUT_IMG.br"
file "$OUT_IMG"
