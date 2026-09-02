#!/bin/bash
# ydraw FFI demo wrapper — runs python/groups.py through the ydraw
# client-interface bindings: named entity groups replaced in place, deleted
# in a timed loop, and re-added as fresh content. Checks the FFI library +
# python toolchain first; emits YDRAW_BIN envelopes on stdout (run inside
# yetty to watch the panels animate, vanish, and cycle).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/groups.py"
