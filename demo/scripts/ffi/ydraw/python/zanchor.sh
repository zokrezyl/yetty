#!/bin/bash
# ydraw FFI demo wrapper — runs python/zanchor.py through the ydraw
# client-interface bindings: three overlapping panels — BACK shapes, MID
# shapes, and a FRONT yplot COMPLEX — where only MID is ever re-emitted.
# Its primitives must stay sandwiched under the complex through 40
# reopens (the replacement-anchor rule), climb ABOVE the plot at z=+1
# (the interleave a prims-first renderer cannot produce), sink below
# BACK at z=-1, return to the exact middle slot at z=0, and vanish on
# DELETE without disturbing the others. Checks the FFI library + python
# toolchain first; emits YDRAW_BIN envelopes on stdout (run inside yetty
# to watch the pulse stay sandwiched under the plot).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/zanchor.py"
