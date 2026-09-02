#!/bin/bash
# ydraw FFI demo wrapper — runs python/zplot.py through the ydraw
# client-interface bindings: `layer` is the ONE uniform z-order attribute on
# primitives, text AND complexes (Box(layer=3), Plot(layer=5)). One list with
# two plots and two boxes at layer=-1/1/3/5, added in scrambled order; the
# receiver's (layer, sequence) sort interleaves the plots between the boxes.
# Checks the FFI library + python toolchain first; emits YDRAW_BIN envelopes
# on stdout (run inside yetty to see the plots stack at their layer).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/zplot.py"
