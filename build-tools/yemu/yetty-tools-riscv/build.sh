#!/usr/bin/env bash
# Pipeline entry point for the `yetty-tools-riscv-<version>` release.
#
# Builds the riscv64 demos+tools (via `make build-linux-riscv-ytrace-release`,
# which also invokes make-riscv-disk.sh as its last step) and packages the
# resulting .img.br into a versioned tarball under $OUTPUT_DIR.
#
# Same shape as build-tools/3rdparty/<lib>/build.sh: read `./version`,
# emit `<name>-<version>.tar.gz`. The .github/workflows/build-yetty-tools-riscv.yml
# release job uploads that tarball as the release asset.
#
# Env vars:
#   OUTPUT_DIR    required — destination for the produced tarball
#   WORK_DIR      optional — repo working tree (default: derived from $0)
#
# Local-dev path: `make build-linux-riscv-ytrace-release` already produces
# yetty-riscv-disk.img and .img.br in the build dir; this wrapper is only
# needed for the release pipeline (which expects a versioned tarball).

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"

VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }

: "${OUTPUT_DIR:?OUTPUT_DIR is required}"
mkdir -p "$OUTPUT_DIR"

cd "$REPO_ROOT"

#-----------------------------------------------------------------------------
# Build riscv64 binaries + disk image. The make target chains disk-linux-
# riscv-ytrace-release as its last step, so this leaves both the raw .img
# and the .img.br in $BUILD_DIR.
#-----------------------------------------------------------------------------
echo "==> make build-linux-riscv-ytrace-release"
make build-linux-riscv-ytrace-release

BUILD_DIR="$REPO_ROOT/build-linux-riscv-ytrace-release"
IMG_BR="$BUILD_DIR/yetty-riscv-disk.img.br"
[ -f "$IMG_BR" ] || { echo "expected $IMG_BR after build" >&2; exit 1; }

#-----------------------------------------------------------------------------
# Package: yetty-tools-riscv-<version>.tar.gz containing the brotli image
# and a small manifest. Manifest is a sanity aid for consumers that pull
# the tarball without context — names the file inside and the source rev.
#-----------------------------------------------------------------------------
STAGE="$(mktemp -d -t yetty-tools-riscv-stage-XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT

cp "$IMG_BR" "$STAGE/yetty-riscv-disk.img.br"
GIT_REV="$(git -C "$REPO_ROOT" rev-parse HEAD)"
cat > "$STAGE/manifest.txt" <<EOF
yetty-tools-riscv $VERSION
git-rev: $GIT_REV
contains: yetty-riscv-disk.img.br (brotli q6 of an ext4 image)
  /yetty/bin   — riscv64 demos + tools built from this rev
  /yetty/repo  — clean \`git archive HEAD\` of this rev
mount in guest:
  brotli -d yetty-riscv-disk.img.br -o yetty-riscv-disk.img
  mount -o loop yetty-riscv-disk.img /mnt
EOF

OUT_TARBALL="$OUTPUT_DIR/yetty-tools-riscv-${VERSION}.tar.gz"
echo "==> packaging -> $OUT_TARBALL"
tar -C "$STAGE" -czf "$OUT_TARBALL" .

echo
echo "yetty-tools-riscv ${VERSION} ready:"
ls -lh "$OUT_TARBALL"
tar -tzf "$OUT_TARBALL"
