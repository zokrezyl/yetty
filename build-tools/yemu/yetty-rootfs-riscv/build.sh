#!/bin/bash
# Pipeline entry point for the unified `yetty-rootfs-riscv-<version>` release.
#
# Produces ONE bootable ext4 image:
#   - alpine-extended-disk pristine rootfs as the base userland
#   - /yetty/bin   — riscv64 yetty demos+tools, **cross-compiled on the host**
#                    via `make build-linux-riscv-ytrace-release`
#   - /yetty/repo  — clean `git archive HEAD` of this rev
#
# Replaces the old `yetty-tools-riscv` + `alpine-extended-disk` consumer
# pair: the cross-compiled binaries used to ship in a small dedicated
# disk; now they're layered on top of the alpine-extended rootfs in one
# bootable image. The toolchain stays on the host (gcc-riscv64-linux-gnu);
# the shipped image is pristine alpine + /yetty/ on top — no toolchain
# pollution.
#
# Env vars:
#   VERSION                  derived — read from ./version (output filename only)
#   OUTPUT_DIR               required — destination for the produced tarball
#   WORK_DIR                 optional — intermediate build tree
#                                       (default: fresh mktemp -d)
#   CACHE_DIR                optional — download cache
#                                       (default: $HOME/.cache/yetty-3rdparty)
#   BROTLI_QUALITY           optional — brotli compression level (default: 6)
#   YETTY_3RDPARTY_URL_BASE  optional — release URL prefix
#                                       (default: https://github.com/zokrezyl/yetty/releases/download)
#
# Needs: curl, brotli, e2fsprogs (mke2fs, e2fsck, resize2fs, dumpe2fs,
# debugfs), tar, gzip, git, plus whatever `make build-linux-riscv-ytrace-release`
# needs (riscv64-linux-gnu-gcc cross toolchain). No sudo, no losetup,
# no mount.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"

# Shared image-manipulation helpers: e2fsck_ok, inject_file, inject_tree,
# fetch. All sudo-free (debugfs-only). See file header for details.
. "$REPO_ROOT/build-tools/yemu/image-helpers.sh"

#-----------------------------------------------------------------------------
# Versioning + paths
#-----------------------------------------------------------------------------

VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

ALPINE_EXT_VER_FILE="$REPO_ROOT/build-tools/3rdparty/alpine-extended-disk/version"
[ -f "$ALPINE_EXT_VER_FILE" ] || { echo "missing $ALPINE_EXT_VER_FILE" >&2; exit 1; }
ALPINE_EXT_VER="$(tr -d '[:space:]' < "$ALPINE_EXT_VER_FILE")"
[ -n "$ALPINE_EXT_VER" ]      || { echo "$ALPINE_EXT_VER_FILE is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-$(mktemp -d -t yetty-rootfs-riscv-XXXXXX)}"
export CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"
URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"
: "${BROTLI_QUALITY:=6}"

ALPINE_EXT_URL="$URL_BASE/lib-alpine-extended-disk-${ALPINE_EXT_VER}/alpine-extended-disk-${ALPINE_EXT_VER}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# Step 1 — Cross-compile yetty riscv64 binaries on the host.
# The make target wraps cmake with the riscv64-linux-gnu-* toolchain
# prefix and produces ELFs under build-linux-riscv-ytrace-release/.
#-----------------------------------------------------------------------------

echo "==> make build-linux-riscv-ytrace-release"
make -C "$REPO_ROOT" build-linux-riscv-ytrace-release

BUILD_DIR="$REPO_ROOT/build-linux-riscv-ytrace-release"

# Binaries to ship — same set the old make-riscv-disk.sh produced. Keep
# in sync with LINUX_RISCV_TARGETS in the top-level Makefile.
BINARIES=(
    "$BUILD_DIR/tools/decode-ypaint/decode-ypaint"
    "$BUILD_DIR/tools/msdf/cdb-viewer/cdb-viewer"
    "$BUILD_DIR/tools/msdf/cdb-diff/cdb-diff"
    "$BUILD_DIR/tools/msdf/gen-msdf/yetty-msdf-gen"
    "$BUILD_DIR/src/yetty/ymsdf-gen/yetty-ymsdf-gen"
    "$BUILD_DIR/temu"
    "$BUILD_DIR/demo/ymgui/01_demo_window/demo-ymgui-01-demo-window"
    "$BUILD_DIR/tools/yplot/yplot"
    "$BUILD_DIR/tools/ymesh/ymesh"
    "$BUILD_DIR/tools/yecho/yecho"
    "$BUILD_DIR/tools/ycat/ycat"
    "$BUILD_DIR/tools/ygreeter/ygreeter"
)
for b in "${BINARIES[@]}"; do
    [ -f "$b" ] || { echo "missing binary: $b — make build-linux-riscv-ytrace-release didn't produce it" >&2; exit 1; }
done

#-----------------------------------------------------------------------------
# Step 2 — Stage /yetty/{bin,repo} content on the host.
#-----------------------------------------------------------------------------

STAGE="$WORK_DIR/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/yetty/bin" "$STAGE/yetty/repo"

echo "==> staging riscv64 binaries"
for b in "${BINARIES[@]}"; do
    cp "$b" "$STAGE/yetty/bin/"
done

echo "==> staging git archive HEAD"
git -C "$REPO_ROOT" archive HEAD | tar -x -C "$STAGE/yetty/repo"
# Drop heavy already-compressed assets brotli can't shrink (matches the
# old make-riscv-disk.sh exclusions: ~21 MiB GIF + ~10 MiB raw TTFs).
rm -f "$STAGE/yetty/repo/docs/pres.gif"
rm -f "$STAGE/yetty/repo/assets/fonts/"*.ttf 2>/dev/null || true

#-----------------------------------------------------------------------------
# Step 3 — Fetch the pristine alpine-extended-disk rootfs.
#-----------------------------------------------------------------------------

ALPINE_EXT_TARBALL="$CACHE_DIR/alpine-extended-disk-${ALPINE_EXT_VER}.tar.gz"
fetch "$ALPINE_EXT_URL" "$ALPINE_EXT_TARBALL" "alpine-extended-disk ${ALPINE_EXT_VER}" rootfs-alpine-ext

ALPINE_EXTRACT="$WORK_DIR/alpine-extract"
rm -rf "$ALPINE_EXTRACT"
mkdir -p "$ALPINE_EXTRACT"
tar -C "$ALPINE_EXTRACT" -xzf "$ALPINE_EXT_TARBALL"

PRISTINE_BR="$ALPINE_EXTRACT/alpine-extended-rootfs.img.br"
[ -f "$PRISTINE_BR" ] || { echo "missing $PRISTINE_BR in alpine tarball" >&2; exit 1; }

PRISTINE_IMG="$WORK_DIR/alpine-extended-rootfs.img"
brotli -d -f -o "$PRISTINE_IMG" "$PRISTINE_BR"

#-----------------------------------------------------------------------------
# Step 4 — Copy pristine to final image, grow enough to fit /yetty,
# inject /yetty via debugfs.
#-----------------------------------------------------------------------------

FINAL_IMG="$WORK_DIR/yetty-rootfs-riscv.img"
cp "$PRISTINE_IMG" "$FINAL_IMG"

# Compute headroom: staged /yetty content + 30% slack for ext4 metadata
# (inode tables, directory blocks, indirect blocks) + 64 MiB floor.
# resize2fs -M at the end gives us the actual minimum back.
STAGE_BYTES="$(du -sb "$STAGE" | awk '{print $1}')"
HEADROOM_MIB=$(( (STAGE_BYTES * 13 / 10 + 64 * 1024 * 1024) / (1024 * 1024) ))
PRISTINE_BYTES="$(stat -c%s "$PRISTINE_IMG")"
PRISTINE_MIB=$(( PRISTINE_BYTES / (1024 * 1024) ))
TARGET_MIB=$(( PRISTINE_MIB + HEADROOM_MIB ))
echo "==> growing image to ${TARGET_MIB} MiB (pristine ${PRISTINE_MIB} + headroom ${HEADROOM_MIB})"
truncate -s "${TARGET_MIB}M" "$FINAL_IMG"
e2fsck_ok "$FINAL_IMG"
resize2fs "$FINAL_IMG"

echo "==> injecting /yetty (bin+repo) into image"
inject_tree "$FINAL_IMG" "$STAGE/yetty" /yetty

# Layer the yetty-rootfs-riscv-specific fs/ overlay onto the pristine
# (e.g. /home/yetty/.bash_profile pointing at /yetty/bin/ygreeter, which
# only exists on the merged image). Files in this overlay overwrite
# whatever alpine-extended-disk ships at the same paths — that's the
# point of putting these customizations in the merger instead of bumping
# alpine-extended-disk for every shell-side tweak.
OVERLAY_DIR="$SCRIPT_DIR/fs"
if [ -d "$OVERLAY_DIR" ] && [ -n "$(LC_ALL=C find "$OVERLAY_DIR" -mindepth 1 -print -quit 2>/dev/null)" ]; then
    echo "==> overlaying yetty-rootfs-riscv/fs onto image"
    inject_tree "$FINAL_IMG" "$OVERLAY_DIR" /
fi

#-----------------------------------------------------------------------------
# Step 5 — Shrink to minimum size, brotli-compress, package.
#-----------------------------------------------------------------------------

echo "==> shrinking image to minimum"
e2fsck_ok "$FINAL_IMG"
resize2fs -M "$FINAL_IMG" 2>&1 | tail -1
BLOCK_COUNT="$(dumpe2fs -h "$FINAL_IMG" 2>/dev/null | awk -F: '/Block count/{gsub(/ /,"",$2); print $2}')"
BLOCK_SIZE="$(dumpe2fs -h "$FINAL_IMG"  2>/dev/null | awk -F: '/Block size/{gsub(/ /,"",$2);  print $2}')"
NEW_BYTES=$(( BLOCK_COUNT * BLOCK_SIZE ))
truncate -s "$NEW_BYTES" "$FINAL_IMG"

echo "==> brotli yetty-rootfs-riscv.img (quality $BROTLI_QUALITY)"
STAGE_OUT="$WORK_DIR/stage-out"
rm -rf "$STAGE_OUT"
mkdir -p "$STAGE_OUT"
in_size="$(stat -c%s "$FINAL_IMG")"
brotli -q "$BROTLI_QUALITY" -f -o "$STAGE_OUT/yetty-rootfs-riscv.img.br" "$FINAL_IMG"
out_size="$(stat -c%s "$STAGE_OUT/yetty-rootfs-riscv.img.br")"
printf "    yetty-rootfs-riscv.img  %12d -> %12d bytes (%.1f%%)\n" \
    "$in_size" "$out_size" \
    "$(awk -v a="$out_size" -v b="$in_size" 'BEGIN{printf "%.1f", a/b*100}')"

GIT_REV="$(git -C "$REPO_ROOT" rev-parse HEAD)"
cat > "$STAGE_OUT/manifest.txt" <<EOF
yetty-rootfs-riscv $VERSION
git-rev: $GIT_REV
base: alpine-extended-disk $ALPINE_EXT_VER
contains: yetty-rootfs-riscv.img.br (brotli q$BROTLI_QUALITY of an ext4 image)
  /                    — pristine alpine-extended-disk rootfs
  /yetty/bin           — riscv64 demos + tools, cross-compiled at this rev
  /yetty/repo          — \`git archive HEAD\` of this rev
mount in guest:
  brotli -d yetty-rootfs-riscv.img.br -o yetty-rootfs-riscv.img
  mount -o loop yetty-rootfs-riscv.img /mnt
EOF

OUT_TARBALL="$OUTPUT_DIR/yetty-rootfs-riscv-${VERSION}.tar.gz"
echo "==> packaging -> $OUT_TARBALL"
tar -C "$STAGE_OUT" -czf "$OUT_TARBALL" .

echo
echo "yetty-rootfs-riscv $VERSION ready:"
ls -lh "$OUT_TARBALL"
tar -tzf "$OUT_TARBALL"
