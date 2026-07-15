#!/bin/bash
# Records the "sci tour" clip: yetty for the scientific community - every
# act on real data, in one unbroken scrollback.
#
#   ./demo/scripts/yctl/clips/sci-tour.sh
#   # -> tmp/clips/sci-tour/sci-tour.mp4
#
# Plays demo/assets/yctl/clips/sci-tour.yaml against a dedicated --record
# yetty. Stages the self-driving body script as ./sci-tour.sh and
# best-effort fetches the record-time-only assets (the GW150914 discovery
# paper PDF) into the recording cwd; the datasets themselves are committed
# snapshots under demo/assets/yscience/ (see its README.md), so an offline
# run only loses the paper and satellite acts.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/sci-tour.yaml"
STAGING_DIR="$ROOT/tmp/clips/sci-tour"

mkdir -p "$STAGING_DIR"
cp "$ROOT/demo/assets/yctl/clips/sci-tour-body.sh" "$STAGING_DIR/sci-tour.sh"
chmod +x "$STAGING_DIR/sci-tour.sh"
rm -f "$STAGING_DIR/tour-done"

# Record-time-only downloads (best-effort; the tour skips what is missing).
"$ROOT/demo/assets/yscience/fetch-data.sh" "$STAGING_DIR" || true

# Point-cloud act asset: a 2000-point helix as a vertex-only binary PLY,
# generated into the staging dir (synthetic by design — no license text
# needed, and it exercises the ymesh point-cloud path).
python3 - "$STAGING_DIR/helix.ply" <<'EOF' || true
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

export YETTY_REPO="$ROOT"
export YETTY_BUILD_DIR="$BUILD_DIR"

record_clip "$SCENARIO" "$STAGING_DIR" "sci-tour.mp4"
