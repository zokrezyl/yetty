#!/bin/bash
# Build OpenSBI RISC-V firmware and produce opensbi-${VERSION}.tar.gz
#
# Env vars:
#   VERSION      derived — read from ./version, used in output filename
#   OUTPUT_DIR   required — where to place the tarball
#   WORK_DIR     optional — intermediate build tree (default: /tmp/yetty-asset-opensbi)
#   CROSS_COMPILE     optional — toolchain prefix (default: riscv64-linux-gnu-)
#
# The `version` file format is <upstream>-<pkg-rev>, e.g. `1.4-1` — single
# source of truth for both the upstream OpenSBI tag fetched here AND the
# lib-opensbi-<version> release tag / opensbi-<version>.tar.gz tarball
# name. Bump <pkg-rev> for packaging-only changes; bump <upstream> when
# moving to a new OpenSBI release.
#
# Hermetic Linux build: needs only make, wget/curl, tar, and the RISC-V
# cross-toolchain. No host-arch-specific output — the firmware is RISC-V
# regardless of where this script runs.

set -euo pipefail

VERSION_FILE="$(dirname "$0")/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

# Split <upstream>-<pkg-rev>. Upstream OpenSBI tags don't contain `-`,
# but split greedily off the right anyway for symmetry with sibling
# producers and forwards-compat with rc tags.
OPENSBI_VERSION="${VERSION%-*}"
PKG_REV="${VERSION##*-}"
[ "$OPENSBI_VERSION" != "$VERSION" ] && [ -n "$PKG_REV" ] || {
    echo "$VERSION_FILE: expected <upstream>-<rev>, got '$VERSION'" >&2
    exit 1
}

WORK_DIR="${WORK_DIR:-/tmp/yetty-asset-opensbi}"
CROSS_COMPILE="${CROSS_COMPILE:-riscv64-linux-gnu-}"

NCPU="$(nproc 2>/dev/null || echo 4)"

SRC_URL="https://github.com/riscv-software-src/opensbi/archive/refs/tags/v${OPENSBI_VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR"
cd "$WORK_DIR"

if [ ! -f "opensbi-${OPENSBI_VERSION}.tar.gz" ]; then
    echo "==> downloading OpenSBI ${OPENSBI_VERSION}"
    curl -fL --retry 3 -o "opensbi-${OPENSBI_VERSION}.tar.gz" "$SRC_URL"
fi

if [ ! -d "opensbi-${OPENSBI_VERSION}" ]; then
    echo "==> extracting"
    tar xf "opensbi-${OPENSBI_VERSION}.tar.gz"
fi

cd "opensbi-${OPENSBI_VERSION}"

echo "==> building (CROSS_COMPILE=${CROSS_COMPILE}, -j${NCPU})"
# -std=gnu11: OpenSBI 1.4 typedefs `bool`, which GCC 15 (default C23) rejects.
# Pass via platform-genflags-y so we don't clobber the Makefile's CFLAGS.
make -j"$NCPU" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    PLATFORM=generic \
    FW_JUMP=y \
    platform-genflags-y="-std=gnu11"

FW_JUMP="build/platform/generic/firmware/fw_jump.elf"
FW_DYNAMIC="build/platform/generic/firmware/fw_dynamic.bin"
[ -f "$FW_JUMP" ]    || { echo "missing $FW_JUMP"    >&2; exit 1; }
[ -f "$FW_DYNAMIC" ] || { echo "missing $FW_DYNAMIC" >&2; exit 1; }

STAGE="$WORK_DIR/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"

# Brotli q11 — embed pipeline picks them up pre-compressed; runtime
# path mode (tinyemu_copy_runtime_to_bundle) gets a decompressed copy
# side-by-side at consumer-side fetch time (3rdparty-fetch.cmake).
: "${BROTLI_QUALITY:=11}"
echo "==> brotli opensbi binaries (quality $BROTLI_QUALITY)"
for src_dst in \
    "$FW_JUMP|opensbi-fw_jump.elf.br" \
    "$FW_DYNAMIC|opensbi-fw_dynamic.bin.br"; do
    src="${src_dst%%|*}"; dst="${src_dst##*|}"
    in_size="$(stat -c%s "$src" 2>/dev/null || stat -f%z "$src")"
    brotli -q "$BROTLI_QUALITY" -f -o "$STAGE/$dst" "$src"
    out_size="$(stat -c%s "$STAGE/$dst" 2>/dev/null || stat -f%z "$STAGE/$dst")"
    printf "    %-30s  %10d -> %10d bytes\n" "$dst" "$in_size" "$out_size"
done

TARBALL="$OUTPUT_DIR/opensbi-${VERSION}.tar.gz"
echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "OpenSBI asset ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
