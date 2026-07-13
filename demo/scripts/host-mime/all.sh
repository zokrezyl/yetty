#!/bin/bash
# host-mime — run every per-type demo in sequence. Each file is shipped RAW
# in a YETTY_DCS_MIME_FILE envelope and rendered by the host yetty.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/all.sh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"

for demo in markdown svg pdf image music circuit mesh; do
    "$DIR/$demo.sh"
    echo
done
