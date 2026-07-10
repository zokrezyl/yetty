#!/bin/bash
# Records the "Markdown in scrollback" clip.
#
#   ./demo/scripts/yctl/clips/markdown-in-scrollback.sh
#   # -> tmp/clips/markdown-in-scrollback/markdown-in-scrollback.mp4

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/markdown-in-scrollback.yaml"
STAGING_DIR="$ROOT/tmp/clips/markdown-in-scrollback"
MARKDOWN_DOC="${MARKDOWN_DOC:-$ROOT/demo/assets/ymarkdown/overview.md}"

if [ ! -f "$MARKDOWN_DOC" ]; then
    echo "Markdown document not found: $MARKDOWN_DOC" >&2
    exit 1
fi

mkdir -p "$STAGING_DIR"
cp "$MARKDOWN_DOC" "$STAGING_DIR/overview.md"

record_clip "$SCENARIO" "$STAGING_DIR" "markdown-in-scrollback.mp4"
