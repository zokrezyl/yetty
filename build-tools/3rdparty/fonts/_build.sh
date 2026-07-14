#!/bin/bash
# Builds the complete font asset set into fonts-${VERSION}.tar.gz.
# Platform-independent (.noarch) — one tarball serves every yetty target.
#
# Contents (one flat directory, consumed by two staging globs):
#   *.cdb.br  — MSDF CDB atlases for the DejaVuSansMNerdFontMono faces,
#               generated here with yetty-ymsdf-gen and brotli'd; the main
#               build embeds them verbatim (msdf-fonts staging glob).
#   *.ttf     — the Noto world-coverage set (script faces + CJK + Color
#               Emoji), harvested from installed distro font packages;
#               staged into the runtime fonts dir where the terminal's
#               codepoint-range routing resolves them by name:
#                 Noto<Family>-Regular.ttf — matched by the "Noto*" chain
#                 NotoSansCJK-Regular.ttf  — renamed .ttc (FreeType sniffs
#                                            the container format)
#                 NotoColorEmoji.ttf       — bare name, no style suffix
#
# Env vars:
#   VERSION           read from ./version — single source of truth
#   OUTPUT_DIR        required — where to place the tarball
#   NOTO_SOURCE_DIRS  optional — colon-separated dirs to harvest Noto from
#                     (default: the Debian/Ubuntu font package locations)
#   WORK_DIR          optional — intermediate tree (default: /tmp/yetty-asset-fonts)
#   BROTLI_QUALITY    optional — CDB compression (default 11)
#
# Run via the wrapper build.sh. Toolchain needs: cmake, ninja, a C/C++23
# compiler, git, curl, brotli — plain distro packages; the CI workflow
# apt-installs ninja/brotli plus the Noto font packages.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

VERSION_FILE="$(dirname "$0")/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
WORK_DIR="${WORK_DIR:-/tmp/yetty-asset-fonts}"
NOTO_SOURCE_DIRS="${NOTO_SOURCE_DIRS:-/usr/share/fonts/truetype/noto:/usr/share/fonts/opentype/noto}"

NCPU="$(nproc 2>/dev/null || echo 4)"

FONT_DIR="$REPO_ROOT/assets/fonts"
TTF_FILES=(
    "$FONT_DIR/DejaVuSansMNerdFontMono-Regular.ttf"
    "$FONT_DIR/DejaVuSansMNerdFontMono-Bold.ttf"
    "$FONT_DIR/DejaVuSansMNerdFontMono-Oblique.ttf"
    "$FONT_DIR/DejaVuSansMNerdFontMono-BoldOblique.ttf"
)

for f in "${TTF_FILES[@]}"; do
    [ -f "$f" ] || { echo "missing TTF: $f" >&2; exit 1; }
done

mkdir -p "$WORK_DIR" "$OUTPUT_DIR"

BUILD_DIR="$WORK_DIR/build"
STAGE="$WORK_DIR/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"

#-----------------------------------------------------------------------------
# Part 1 — MSDF CDB atlases for the base terminal faces.
#-----------------------------------------------------------------------------

echo "==> configuring host tools"
cmake -S "$REPO_ROOT/build-tools/yetty/host-tools" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release

echo "==> building yetty-ymsdf-gen (-j${NCPU})"
cmake --build "$BUILD_DIR" --target yetty-ymsdf-gen --parallel "$NCPU"

YMSDF_GEN="$BUILD_DIR/yetty-ymsdf-gen"
[ -x "$YMSDF_GEN" ] || { echo "missing binary: $YMSDF_GEN" >&2; exit 1; }

RAW_DIR="$WORK_DIR/cdb-raw"
rm -rf "$RAW_DIR"
mkdir -p "$RAW_DIR"

for ttf in "${TTF_FILES[@]}"; do
    echo "==> generating CDB for $(basename "$ttf")"
    "$YMSDF_GEN" --all "$ttf" "$RAW_DIR"
done

# Sanity check — expect one .cdb per TTF
CDB_COUNT="$(find "$RAW_DIR" -maxdepth 1 -name '*.cdb' | wc -l)"
if [ "$CDB_COUNT" -ne "${#TTF_FILES[@]}" ]; then
    echo "expected ${#TTF_FILES[@]} cdb files, got $CDB_COUNT" >&2
    ls -la "$RAW_DIR" >&2
    exit 1
fi

# Brotli-compress each CDB into the stage. The main build's incbin
# pipeline embeds these verbatim and decompresses at runtime (same
# scheme used by build-tools/cmake/incbin.cmake with COMPRESS=TRUE).
# q11 = max compression. CI runs once per release; runtime decompresses
# in-binary at startup, so producer-side cost is amortised forever.
: "${BROTLI_QUALITY:=11}"
for cdb in "$RAW_DIR"/*.cdb; do
    name="$(basename "$cdb")"
    in_size="$(stat -c%s "$cdb" 2>/dev/null || stat -f%z "$cdb")"
    echo "==> brotli ${name} (quality $BROTLI_QUALITY)"
    brotli -q "$BROTLI_QUALITY" -f -o "$STAGE/${name}.br" "$cdb"
    out_size="$(stat -c%s "$STAGE/${name}.br" 2>/dev/null || stat -f%z "$STAGE/${name}.br")"
    printf "    %-50s  %10d -> %10d bytes\n" "$name" "$in_size" "$out_size"
done

#-----------------------------------------------------------------------------
# Part 2 — Noto world-coverage set.
#-----------------------------------------------------------------------------

script_count=0
have_emoji=0
have_cjk=0

IFS=':' read -r -a source_dirs <<< "$NOTO_SOURCE_DIRS"
for source_dir in "${source_dirs[@]}"; do
    [ -d "$source_dir" ] || continue

    # Script faces: every Regular weight, matched by the "Noto*" chain.
    for font_file in "$source_dir"/Noto*-Regular.ttf; do
        [ -f "$font_file" ] || continue
        base_name="$(basename "$font_file")"
        if [ ! -f "$STAGE/$base_name" ]; then
            cp "$font_file" "$STAGE/$base_name"
            script_count=$((script_count + 1))
        fi
    done

    # Color emoji (bare name — single-style file).
    if [ -f "$source_dir/NotoColorEmoji.ttf" ] && [ ! -f "$STAGE/NotoColorEmoji.ttf" ]; then
        cp "$source_dir/NotoColorEmoji.ttf" "$STAGE/NotoColorEmoji.ttf"
        have_emoji=1
    fi

    # CJK collection: ship the .ttc renamed to the name the resolver loads.
    if [ -f "$source_dir/NotoSansCJK-Regular.ttc" ] && [ ! -f "$STAGE/NotoSansCJK-Regular.ttf" ]; then
        cp "$source_dir/NotoSansCJK-Regular.ttc" "$STAGE/NotoSansCJK-Regular.ttf"
        have_cjk=1
    fi
done

echo "==> staged: $script_count Noto script faces, emoji=$have_emoji cjk=$have_cjk"
[ "$script_count" -ge 100 ] || { echo "too few script fonts ($script_count) — wrong source dirs?" >&2; exit 1; }
[ "$have_emoji" = 1 ] || { echo "NotoColorEmoji.ttf not found in $NOTO_SOURCE_DIRS" >&2; exit 1; }
[ "$have_cjk" = 1 ] || { echo "NotoSansCJK-Regular.ttc not found in $NOTO_SOURCE_DIRS" >&2; exit 1; }

#-----------------------------------------------------------------------------
# Package.
#-----------------------------------------------------------------------------

TARBALL="$OUTPUT_DIR/fonts-${VERSION}.tar.gz"
echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "fonts asset ready:"
ls -lh "$TARBALL"
