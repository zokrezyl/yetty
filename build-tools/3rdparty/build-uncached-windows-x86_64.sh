#!/usr/bin/env bash
# Build all uncached 3rdparty libs for windows-x86_64 (native MSVC).
#
# Run from a Developer Command Prompt for VS (so cl.exe is on PATH) inside
# an MSYS2 / Git Bash shell that inherits the vcvars env. Each per-lib
# build.sh wrapper enforces the cl-on-PATH check.
exec env TARGET_PLATFORM=windows-x86_64 "$(dirname "$0")/build-uncached.sh" "$@"
