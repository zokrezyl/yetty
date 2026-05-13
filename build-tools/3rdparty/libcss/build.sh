#!/usr/bin/env bash
# libcss 3rdparty wrapper. Builds NetSurf's MIT-licensed CSS cascade
# (libcss + libparserutils + libwapcaplet) standalone — no GPL'd NetSurf
# core, no curl/xml/png/jpeg/webp pulls.
#
# Toolchain expectations:
#
#   linux-x86_64   — native on a stock ubuntu runner; apt installs
#                    gcc + make + perl + pkg-config + flex/bison/gperf.
#   linux-aarch64  — native on GitHub's ubuntu-24.04-arm runner; same
#                    apt set, /usr/bin/gcc.
#   linux-riscv64  — cross-compile from ubuntu-latest; apt installs
#                    gcc-riscv64-linux-gnu + flex/bison/gperf. The only
#                    linux target that cross-compiles.
#   macos-arm64,   — native on a macOS runner with Xcode CommandLineTools;
#   macos-x86_64     brew installs flex/bison/gperf.
#   ios-*, tvos-*, — NetSurf's GNU-make buildsystem doesn't honour CC/HOST
#   android-*,       for these triples; _build.sh writes a placeholder
#   webasm,          UNSUPPORTED tarball. The consumer-side libcss.cmake
#   windows-x86_64   detects the marker and falls back to lexbor-CSS.

set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"

exec bash "$(dirname "$0")/_build.sh" "$@"
