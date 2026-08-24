#!/bin/bash
# ydraw FFI demo wrapper — runs python/yshadertoy/shaders.py through the ydraw client-interface
# bindings. Checks the FFI library + python toolchain first; emits
# YDRAW_BIN envelopes on stdout (run inside yetty to see the drawing).
source "$(dirname "$0")/../../common.sh"
ffi_run_python "python/yshadertoy/shaders.py"
