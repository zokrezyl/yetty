#!/bin/bash
# Stages a per-platform miniaudio tarball. miniaudio is a single-header
# library — the same miniaudio.h serves every backend (WASAPI on Windows,
# CoreAudio on Apple, ALSA / PulseAudio on Linux, AAudio / OpenSL ES on
# Android, WebAudio on Emscripten); the backend is selected inside the
# header at compile time. We still publish one tarball per TARGET_PLATFORM
# so the file naming matches every other 3rdparty producer in this tree
# (lz4, bzip2, etc.) and cmake's yetty_3rdparty_fetch can resolve
# `miniaudio-<platform>-<version>.tar.gz` uniformly.
#
# Required env:
#   TARGET_PLATFORM  used as the platform slug in the output tarball name
#   OUTPUT_DIR       where the tarball is written
#
# Version: this directory's `version` file holds the bare X.Y.Z tag
# (mackron/miniaudio publishes tags as "0.11.22" — no `v` prefix).
#
# Output tarball layout (consumed by build-tools/yetty/miniaudio.cmake):
#   include/miniaudio.h
#   share/licenses/miniaudio/LICENSE

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO source=${BASH_SOURCE[0]} cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION_FILE="$SCRIPT_DIR/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }

# Per-platform work dirs so concurrent matrix jobs on the same host
# (e.g. the linux-cross matrix) don't trample each other's extract dir.
WORK_DIR="${WORK_DIR:-/tmp/yetty-3rdparty-miniaudio-${TARGET_PLATFORM}}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"

MINIAUDIO_URL="https://github.com/mackron/miniaudio/archive/refs/tags/${VERSION}.tar.gz"
# Distinct from the output tarball name so a cache dir shared with
# OUTPUT_DIR doesn't shadow the upstream source archive with our staged
# output (which previously caused the source to be silently replaced by
# the per-platform tarball after the first run).
MINIAUDIO_TARBALL="$CACHE_DIR/miniaudio-${VERSION}-src.tar.gz"
SRC_DIR="$WORK_DIR/miniaudio-${VERSION}"
STAGE="$WORK_DIR/stage"
TARBALL="$OUTPUT_DIR/miniaudio-${TARGET_PLATFORM}-${VERSION}.tar.gz"

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
echo "miniaudio ${VERSION} (${TARGET_PLATFORM}) ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
