#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
exec nix develop .#assets-alpine-extended --command bash build-tools/3rdparty/alpine-extended-disk/_build.sh "$@"
