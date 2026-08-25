#!/bin/bash
# ydraw FFI demo wrapper — runs go/yimage/gallery.go through the ydraw client-interface
# bindings. Checks the FFI library + go toolchain first; emits
# YDRAW_BIN envelopes on stdout (run inside yetty to see the drawing).
source "$(dirname "$0")/../../common.sh"
ffi_run_go "go/yimage/gallery.go"
