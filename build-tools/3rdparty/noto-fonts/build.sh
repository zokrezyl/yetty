#!/usr/bin/env bash
# Pure file harvesting — no toolchain needed, so no nix develop wrapper.
# The CI workflow installs the distro Noto packages first; locally any
# system with fonts-noto-core + fonts-noto-cjk + fonts-noto-color-emoji
# (or NOTO_SOURCE_DIRS pointing at equivalents) works.
set -euo pipefail
cd "$(dirname "$0")/../../.."
exec bash build-tools/3rdparty/noto-fonts/_build.sh "$@"
