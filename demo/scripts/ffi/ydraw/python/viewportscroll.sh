#!/bin/bash
# ydraw FFI demo wrapper — runs python/viewportscroll.py through the ydraw
# client-interface bindings: the VIEWPORT primitive. reserve(height_px)
# declares the insertion's row span; taller content is sent once and clipped
# by the projection; scrolling = updating the root group's offset (~20 bytes
# per tick, ids stable, out-of-view content stays addressable). Checks the
# FFI library + python toolchain first; emits YDRAW_BIN envelopes on stdout
# (run inside yetty).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/viewportscroll.py"
