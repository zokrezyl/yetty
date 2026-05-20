#!/bin/bash
# Builds the noarch miniaudio tarball — fetches the single-header
# miniaudio.h from mackron/miniaudio@<version> and packages it. No
# compilation: yetty's main build #defines MINIAUDIO_IMPLEMENTATION in
# exactly one TU (src/platform/audio/miniaudio-device.c) and includes
# the header — same STB-style pattern used by stb_image / minimp4 in
# this repo.
#
# Required env:
#   OUTPUT_DIR  where the tarball is written
#
# Version: this directory's `version` file holds the bare X.Y.Z tag
# (mackron/miniaudio publishes tags as "0.11.22" — no `v` prefix).
#
# Output tarball layout (consumed by build-tools/yetty/miniaudio.cmake):
#   include/miniaudio.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-miniaudio}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"

MINIAUDIO_URL="https://github.com/mackron/miniaudio/archive/refs/tags/${VERSION}.tar.gz"
MINIAUDIO_TARBALL="$CACHE_DIR/miniaudio-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/miniaudio-${VERSION}"
STAGE="$WORK_DIR/stage"
TARBALL="$OUTPUT_DIR/miniaudio-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$MINIAUDIO_TARBALL" ]; then
    _part="$MINIAUDIO_TARBALL.part.$$"
    (
        if command -v flock >/dev/null 2>&1; then flock -x 9; fi
        if [ ! -f "$MINIAUDIO_TARBALL" ]; then
            echo "==> downloading miniaudio ${VERSION}"
            curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
                -o "$_part" "$MINIAUDIO_URL"
            mv "$_part" "$MINIAUDIO_TARBALL"
        fi
    ) 9>"$CACHE_DIR/.miniaudio-download.lock"
    rm -f "$_part"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    mkdir -p "$WORK_DIR/.extract-$$"
    tar -C "$WORK_DIR/.extract-$$" -xzf "$MINIAUDIO_TARBALL"
    mv "$WORK_DIR/.extract-$$/miniaudio-${VERSION}" "$SRC_DIR"
    rmdir "$WORK_DIR/.extract-$$"
fi
rm -rf "$STAGE"
mkdir -p "$STAGE/include" "$STAGE/share/licenses/miniaudio"

#-----------------------------------------------------------------------------
# Stage miniaudio.h + license (LICENSE file documents the MIT-0 / Public
# Domain dual offer — required attribution under MIT-0).
#-----------------------------------------------------------------------------
[ -f "$SRC_DIR/miniaudio.h" ] || { echo "missing miniaudio.h in source" >&2; exit 1; }
cp "$SRC_DIR/miniaudio.h" "$STAGE/include/"
if [ -f "$SRC_DIR/LICENSE" ]; then
    cp "$SRC_DIR/LICENSE" "$STAGE/share/licenses/miniaudio/"
fi

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "miniaudio ${VERSION} (noarch) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
