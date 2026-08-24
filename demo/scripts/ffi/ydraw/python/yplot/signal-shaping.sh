#!/bin/bash
# ydraw FFI demo wrapper — runs python/yplot/signal-shaping.py through the ydraw client-interface
# bindings. Checks the FFI library + python toolchain first; emits
# YDRAW_BIN envelopes on stdout (run inside yetty to see the drawing).
source "$(dirname "$0")/../../common.sh"
ffi_run_python "python/yplot/signal-shaping.py"
