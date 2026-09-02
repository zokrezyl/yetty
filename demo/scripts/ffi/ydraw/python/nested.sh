#!/bin/bash
# ydraw FFI demo wrapper — runs python/nested.py through the ydraw
# client-interface bindings: NESTED groups as paths ([100], [100.1], [100.2]).
# Shows exact-subtree replace in place (omit a child and it is gone), local-id
# scoping (a top-level GROUP(1) is the path [1], not [100.1]), and subtree
# delete. Checks the FFI library + python toolchain first; emits YDRAW_BIN
# envelopes on stdout (run inside yetty to watch the dialog).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/nested.py"
