#!/usr/bin/env bash
# The CDB half needs the host toolchain (cmake/ninja/brotli) from the
# assets-cdb dev shell; the Noto half harvests installed distro font
# packages (visible inside nix develop — it is not sandboxed). The CI
# workflow apt-installs fonts-noto-core/-cjk/-color-emoji first.
set -euo pipefail
cd "$(dirname "$0")/../../.."
exec nix develop .#assets-cdb --command bash build-tools/3rdparty/fonts/_build.sh "$@"
