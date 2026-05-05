#!/bin/bash
# Convert an MP4 (e.g. a yetty --record output) to an animated GIF
# suitable for embedding in README.md.
#
# Two-pass ffmpeg pipeline:
#   1. palettegen with stats_mode=diff and reserve_transparent=0 — give
#      the palette to regions that actually change, and reclaim the
#      transparent slot since we never use it (full 256 colors).
#   2. paletteuse with bayer:bayer_scale=3 dithering and no diff_mode.
#      bayer:3 is light ordered dithering that keeps terminal text edges
#      crisp while still avoiding banding on plot gradients. diff_mode
#      is intentionally OFF: it shrinks files but leaves visible
#      rectangular ghosts on text edges around the cursor.
#
# Usage:
#   tools/mp4-to-anim-gif.sh <input.mp4> [output.gif] [width] [fps]
#
# Defaults: output = <input basename>.gif next to the input, width = 1200,
# fps = 15. Override per call:
#   tools/mp4-to-anim-gif.sh tmp/pres.mp4 docs/readme-tour.gif 1440 15
#
# Dither knob via env var (advanced):
#   DITHER='bayer:bayer_scale=3'  # default — sharp, mild dithering
#   DITHER='none'                  # sharpest; may band on gradients
#   DITHER='sierra2_4a'            # smoother gradients; softer text

set -euo pipefail

if [ $# -lt 1 ] || [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    sed -n '2,26p' "$0"
    exit 1
fi

IN="$1"
OUT="${2:-${IN%.*}.gif}"
WIDTH="${3:-1200}"
FPS="${4:-15}"
DITHER="${DITHER:-bayer:bayer_scale=3}"

if [ ! -f "$IN" ]; then
    echo "input not found: $IN" >&2
    exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg not on PATH — install it first" >&2
    exit 1
fi

OUT_DIR="$(dirname "$OUT")"
mkdir -p "$OUT_DIR"

PALETTE="$(mktemp -t mp4-to-anim-gif-palette.XXXXXX.png)"
trap 'rm -f "$PALETTE"' EXIT

# Use even height (-2) so the H.264 source's odd dimensions don't bite
# the scaler. Lanczos resampling preserves edge sharpness better than
# the default bilinear, important for terminal text.
SCALE_FILTER="fps=${FPS},scale=${WIDTH}:-2:flags=lanczos"

echo "==> pass 1/2: palette (${WIDTH}px @ ${FPS}fps, dither=${DITHER})"
ffmpeg -y -hide_banner -loglevel error \
    -i "$IN" \
    -vf "${SCALE_FILTER},palettegen=stats_mode=diff:reserve_transparent=0" \
    "$PALETTE"

echo "==> pass 2/2: encode → $OUT"
ffmpeg -y -hide_banner -loglevel error \
    -i "$IN" -i "$PALETTE" \
    -lavfi "${SCALE_FILTER}[x];[x][1:v]paletteuse=dither=${DITHER}" \
    "$OUT"

SIZE_BYTES=$(stat -c%s "$OUT" 2>/dev/null || stat -f%z "$OUT")
SIZE_MB=$(awk -v b="$SIZE_BYTES" 'BEGIN { printf "%.2f", b / 1048576 }')
echo "==> done: $OUT (${SIZE_MB} MB)"
