#!/bin/bash
# ydraw FFI demo wrapper — runs python/pathstream.py through the ydraw
# client-interface bindings: absolute-path addressing of NESTED complexes.
# Two plots with their own ids inside GROUP(7) — paths [7.10000]/[7.10012];
# dlist.path(prefix) + update/delete addresses exactly one of them at any
# depth. Checks the FFI library + python toolchain first; emits YDRAW_BIN
# envelopes on stdout (run inside yetty).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/pathstream.py"
