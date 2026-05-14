#!/usr/bin/env bash
# openssh 3rdparty wrapper — dispatches directly to _build.sh.
# No nix: toolchain is provided by the CI runner (apt/brew/NDK).
#
# Required env:
#   TARGET_PLATFORM   see _build.sh for the full platform list
#   OUTPUT_DIR        where the tarball is written

set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

exec bash "$(dirname "$0")/_build.sh" "$@"
