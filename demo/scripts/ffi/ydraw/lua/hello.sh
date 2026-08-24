#!/bin/bash
# ydraw FFI demo wrapper — runs lua/hello.lua through the ydraw client-interface
# bindings. Checks the FFI library + lua toolchain first; emits
# YDRAW_BIN envelopes on stdout (run inside yetty to see the drawing).
source "$(dirname "$0")/../common.sh"
ffi_run_lua "lua/hello.lua"
