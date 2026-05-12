#!/usr/bin/env bash
# nghttp3 3rdparty wrapper. See nghttp2/build.sh for context — same
# pattern (native build, no Nix, workflow arranges target-matching
# runner / QEMU container).
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
exec bash "$(dirname "$0")/_build.sh" "$@"
