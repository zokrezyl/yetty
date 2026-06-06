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

# Version tracks the yetty release. The parent CI pipeline sets VERSION
# explicitly when it runs on a yetty-X.Y.Z tag; for branch pushes and
# local builds we derive it from `git describe`. No separate version
# file: the yetty tag is the single source of truth.
if [ -z "${VERSION:-}" ]; then
    TAG="$(git -C "$REPO_ROOT" describe --tags --abbrev=0 \
              --match='yetty-[0-9]*.[0-9]*.[0-9]*' HEAD 2>/dev/null || true)"
    if [ -z "$TAG" ]; then
        echo "FATAL: cannot derive VERSION — no yetty-X.Y.Z tag reachable from HEAD." >&2
        echo "Set VERSION=X.Y.Z explicitly, or `git fetch --tags` and rerun." >&2
        exit 1
    fi
    VERSION="${TAG#yetty-}"
fi
[ -n "$VERSION" ] || { echo "VERSION resolved to empty" >&2; exit 1; }
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
    "$BUILD_DIR/tools/decode-ydraw/decode-ydraw"
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
# Step 2 — Stage user-prefix binaries and the explorable repo tree.
#
# Layout:
#   /home/yetty/.local/bin/    — riscv64 tools (per-user XDG bin prefix).
#                                The tools are thin (no embedded assets);
#                                the assets they need are installed alongside
#                                under /home/yetty/.local/share/yetty and
#                                /home/yetty/.config/yetty during this image
#                                build (see the share/config staging below).
#   /yetty/repo                — `git archive HEAD` of this rev, kept for
#                                the user to explore additional demos +
#                                source code that the embedded assets
#                                don't cover.
#-----------------------------------------------------------------------------

STAGE="$WORK_DIR/stage"
rm -rf "$STAGE"
BIN_STAGE="$STAGE/home/yetty/.local/bin"
REPO_STAGE="$STAGE/yetty/repo"
mkdir -p "$BIN_STAGE" "$REPO_STAGE"

echo "==> staging riscv64 binaries to /home/yetty/.local/bin"
for b in "${BINARIES[@]}"; do
    cp "$b" "$BIN_STAGE/"
done

# Per-user share + config. The riscv tools are thin — they don't embed their
# own assets — so the assets they need are installed straight into the image
# here, the riscv equivalent of what the desktop yinstall installer deploys.
# The VM has no GPU, so shaders/CDBs are skipped; only what the client-mode
# tools actually use is staged: ygreeter's logos + intro video, the demos,
# fonts and config. Everything under $STAGE/home/yetty is injected below by
# the existing /home/yetty inject_tree (uid 1000), so no extra inject needed.
SHARE_STAGE="$STAGE/home/yetty/.local/share/yetty"
CONFIG_STAGE="$STAGE/home/yetty/.config/yetty"
mkdir -p "$SHARE_STAGE/fonts" "$SHARE_STAGE/demos" "$CONFIG_STAGE/temu"

echo "==> staging ygreeter assets + demos + fonts to /home/yetty/.local/share/yetty"
cp "$REPO_ROOT"/assets/logo-*.jpeg                "$SHARE_STAGE/"        2>/dev/null || true
cp "$REPO_ROOT/assets/yetty-unchained-2.mp4"      "$SHARE_STAGE/"        2>/dev/null || true
cp "$REPO_ROOT/test/ut/ypdf/pdf-sample.pdf"       "$SHARE_STAGE/"        2>/dev/null || true
cp "$REPO_ROOT/README.md"                         "$SHARE_STAGE/"        2>/dev/null || true
cp -a "$REPO_ROOT"/demo/.                          "$SHARE_STAGE/demos/"
cp "$REPO_ROOT"/assets/fonts/*.ttf                "$SHARE_STAGE/fonts/"  2>/dev/null || true

echo "==> staging config to /home/yetty/.config/yetty"
cp "$REPO_ROOT/build-tools/yetty/platform/config-defaults.yaml" "$CONFIG_STAGE/config.yaml"
cp "$REPO_ROOT/build-tools/yetty/platform/config-defaults.yaml" "$CONFIG_STAGE/defaults.yaml"
cp "$REPO_ROOT"/assets/yemu/temu/*.cfg            "$CONFIG_STAGE/temu/"  2>/dev/null || true

echo "==> staging git archive HEAD to /yetty/repo"
git -C "$REPO_ROOT" archive HEAD | tar -x -C "$REPO_STAGE"
# Drop heavy already-compressed assets brotli can't shrink (matches the
# old make-riscv-disk.sh exclusions: ~21 MiB GIF + ~10 MiB raw TTFs).
rm -f "$REPO_STAGE/docs/pres.gif"
rm -f "$REPO_STAGE/assets/fonts/"*.ttf 2>/dev/null || true

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
# Step 4 — Copy pristine to final image, grow enough to fit the staged
# trees, inject /yetty/repo (root-owned) and /home/yetty (yetty-owned)
# via debugfs.
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

echo "==> injecting /yetty/repo (root-owned, browsable)"
inject_tree "$FINAL_IMG" "$REPO_STAGE" /yetty/repo

# Per-user bin prefix. inject_tree set_inode_field's every prefix
# component (/home, /home/yetty, /home/yetty/.local, …) — pass uid/gid
# 1000:1000 so /home/yetty stays writable by the yetty user (otherwise
# the tools couldn't create ~/.local/share/yetty at first run).
echo "==> injecting /home/yetty/.local/bin (yetty-owned)"
inject_tree "$FINAL_IMG" "$STAGE/home/yetty" /home/yetty 1000 1000

# Layer the yetty-rootfs-riscv-specific fs/ overlay onto the pristine
# (e.g. /home/yetty/.bash_profile launching ~/.local/bin/ygreeter).
# Scoped per top-level area so each subtree gets the right ownership —
# inject_tree set_inode_field's every prefix component, so a single
# "/" injection with uid=1000 would clobber /home (or anything else
# above the actual file) to yetty-owned.
OVERLAY_DIR="$SCRIPT_DIR/fs"
if [ -d "$OVERLAY_DIR/home/yetty" ]; then
    echo "==> overlaying fs/home/yetty/ onto /home/yetty (yetty-owned)"
    inject_tree "$FINAL_IMG" "$OVERLAY_DIR/home/yetty" /home/yetty 1000 1000
fi
# Future: add per-area branches here (e.g. fs/etc/ → /etc with uid 0)
# when the overlay grows beyond /home/yetty.

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
  /                              — pristine alpine-extended-disk rootfs
  /home/yetty/.local/bin/        — riscv64 tools (per-user XDG prefix);
                                   each binary embeds its own assets via
                                   incbin and extracts to
                                   /home/yetty/.local/share/<tool>/ on
                                   first run — standalone-redistributable
  /yetty/repo                    — \`git archive HEAD\` of this rev, kept
                                   for the user to explore further demos
                                   and source code
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
