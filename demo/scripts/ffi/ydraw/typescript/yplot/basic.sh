#!/bin/bash
# ydraw FFI demo wrapper — runs typescript/yplot/basic.ts through the ydraw client-interface
# bindings. Checks the FFI library + typescript toolchain first; emits
# YDRAW_BIN envelopes on stdout (run inside yetty to see the drawing).
source "$(dirname "$0")/../../common.sh"
ffi_run_typescript "typescript/yplot/basic.ts"
