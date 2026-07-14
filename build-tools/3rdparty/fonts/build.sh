#!/usr/bin/env bash
# Plain toolchain build — cmake/ninja/gcc/brotli from the distro suffice
# (host-tools pulls its own deps via CPM). The CI workflow apt-installs
# ninja-build, brotli and the Noto font packages first; locally any box
# with those plus fonts-noto-core/-cjk/-color-emoji (or NOTO_SOURCE_DIRS
# pointing at equivalents) works.
set -euo pipefail
cd "$(dirname "$0")/../../.."
exec bash build-tools/3rdparty/fonts/_build.sh "$@"
