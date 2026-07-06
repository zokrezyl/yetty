#!/bin/bash
# ybrowser (one-shot) — raster images: render demo/assets/ybrowser/images.html as one
# ydraw OSC envelope scrolling into the terminal.
#
# PNG (demo/assets/yimage) and JPEG (assets/logo-*.jpeg) decoding with
# attribute sizing, CSS sizing, aspect-ratio preservation, and a floated
# image with text wrapping around it.
#
# Entirely offline — the page and its assets are committed in the repo.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ybrowser/once/images.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../.." && pwd)"
YBROWSER="${YBROWSER:-$ROOT/build-desktop-ytrace-release/tools/ybrowser/ybrowser}"
PAGE="$ROOT/demo/assets/ybrowser/images.html"

if [ ! -x "$YBROWSER" ]; then
    YBROWSER="$(command -v "${YBROWSER##*/}" 2>/dev/null || true)"
fi
if [ -z "$YBROWSER" ] || [ ! -x "$YBROWSER" ]; then
    echo "ybrowser binary not found in build dir or on \$PATH — set YBROWSER=path/to/ybrowser" >&2
    exit 1
fi

printf '=== ybrowser — raster images ===\n\n'
echo '$ ybrowser --once demo/assets/ybrowser/images.html'
# --osc: under `yetty -e` stdout is a pipe, not a TTY — force the envelope.
exec "$YBROWSER" --once --osc "$PAGE"
