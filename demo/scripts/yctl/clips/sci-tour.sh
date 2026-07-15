#!/bin/bash
# Records the "sci tour" clip: yetty for the scientific community - every
# act on real data, TYPED live at the prompt by the scenario.
#
#   ./demo/scripts/yctl/clips/sci-tour.sh
#   # -> tmp/clips/sci-tour/sci-tour.mp4
#
# NOTHING IS COPIED OR STAGED: the recording session runs with the REPO
# ROOT as its working directory and the scenario types the real paths —
# committed assets under demo/assets/..., record-time downloads under
# tmp/sci-data/. Tools (and the yrdawn N-body demo) come from PATH via
# _record-common.sh.
#
# This wrapper only:
#   - fetches the record-time downloads into tmp/sci-data (paper PDF,
#     NOAA/GWOSC/USGS files — best-effort, acts skip what is missing)
#   - generates tmp/sci-data/helix.ply (the point-cloud act's synthetic
#     2000-point sample)
#   - pre-warms the uv cache so the matplotlib act doesn't stall on
#     dependency resolution mid-clip

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/sci-tour.yaml"
STAGING_DIR="$ROOT/tmp/clips/sci-tour"
DATA_DIR="$ROOT/tmp/sci-data"

mkdir -p "$STAGING_DIR" "$DATA_DIR"

# Record-time-only downloads (best-effort; acts skip what is missing).
"$ROOT/demo/assets/yscience/fetch-data.sh" "$DATA_DIR" || true

# Point-cloud act asset: a 2000-point helix as a vertex-only binary PLY
# (synthetic by design — exercises the ymesh point-cloud path).
python3 - "$DATA_DIR/helix.ply" <<'EOF' || true
import math, struct, sys
count = 2000
with open(sys.argv[1], 'wb') as out:
    header = ("ply\nformat binary_little_endian 1.0\nelement vertex %d\n"
              "property float x\nproperty float y\nproperty float z\nend_header\n" % count)
    out.write(header.encode())
    for i in range(count):
        angle = i / count * 12 * math.pi
        out.write(struct.pack('<3f', math.cos(angle), angle / 8.0 - 2.4, math.sin(angle)))
EOF

# Pre-warm the uv cache so demo/python/mpl-demo.py resolves matplotlib and
# numpy before the clock is rolling (best-effort).
if command -v uv >/dev/null; then
    timeout 180 uv run --with matplotlib --with numpy \
        python3 -c 'import matplotlib, numpy' >/dev/null 2>&1 || true
fi

export YETTY_REPO="$ROOT"
export YETTY_BUILD_DIR="$BUILD_DIR"

# Fourth argument: run the session from the repo root — the scenario types
# real repo paths, nothing staged.
record_clip "$SCENARIO" "$STAGING_DIR" "sci-tour.mp4" "$ROOT"
