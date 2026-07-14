#!/bin/bash
# Packs the Noto world-coverage font set (script fonts + CJK + Color Emoji)
# into noto-fonts-<VERSION>.tar.gz. Platform-independent (.noarch) — one
# tarball serves every yetty target; the build embeds/stages the files into
# the runtime fonts dir where the terminal's codepoint-range routing (the
# "Noto*" fallback-chain face, NotoSansCJK, NotoColorEmoji — see
# config-defaults.yaml) picks them up by name.
#
# Font sources are directories of installed Noto fonts (CI installs the
# distro packages; see the workflow). Naming rules enforced here match the
# terminal's font-name resolver:
#   Noto<Family>-Regular.ttf   — script faces, matched by the "Noto*" glob
#   NotoSansCJK-Regular.ttf    — the CJK collection (a renamed .ttc;
#                                FreeType sniffs the container format)
#   NotoColorEmoji.ttf         — color emoji (bare name, no style suffix)
#
# Env vars:
#   OUTPUT_DIR        required — where to place the tarball
#   NOTO_SOURCE_DIRS  optional — colon-separated dirs to harvest from
#                     (default: the Debian/Ubuntu font package locations)
#   WORK_DIR          optional — staging tree (default: /tmp/yetty-asset-noto-fonts)

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

VERSION_FILE="$(dirname "$0")/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

NOTO_SOURCE_DIRS="${NOTO_SOURCE_DIRS:-/usr/share/fonts/truetype/noto:/usr/share/fonts/opentype/noto}"
WORK_DIR="${WORK_DIR:-/tmp/yetty-asset-noto-fonts}"

STAGE="$WORK_DIR/stage"
TARBALL="$OUTPUT_DIR/noto-fonts-${VERSION}.tar.gz"

rm -rf "$STAGE"
mkdir -p "$STAGE" "$OUTPUT_DIR"

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

echo "==> staged: $script_count script faces, emoji=$have_emoji cjk=$have_cjk"
[ "$script_count" -ge 100 ] || { echo "too few script fonts ($script_count) — wrong source dirs?" >&2; exit 1; }
[ "$have_emoji" = 1 ] || { echo "NotoColorEmoji.ttf not found in $NOTO_SOURCE_DIRS" >&2; exit 1; }
[ "$have_cjk" = 1 ] || { echo "NotoSansCJK-Regular.ttc not found in $NOTO_SOURCE_DIRS" >&2; exit 1; }

tar -C "$STAGE" -czf "$TARBALL" .
echo "==> $TARBALL ($(du -h "$TARBALL" | cut -f1))"
