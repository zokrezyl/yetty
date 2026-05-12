#!/usr/bin/env bash
# nghttp2 3rdparty wrapper. Native build only — the workflow runs this
# on the matching target runner (linux-aarch64 on ubuntu-24.04-arm,
# linux-riscv64 inside a Docker container under QEMU, etc.). No Nix:
# nghttp2 has zero exotic deps (just autoconf + libtool + a C compiler)
# and cross-compiling autoconf for riscv64/aarch64 caused more pain than
# native emulation/native runners — see _build.sh header.
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
exec bash "$(dirname "$0")/_build.sh" "$@"
