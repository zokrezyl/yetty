#!/bin/bash
# ydraw FFI demo wrapper — runs python/zorder.py through the ydraw
# client-interface bindings: one envelope whose overlapping shapes, text
# runs AND a yplot complex are emitted AGAINST the intended stacking and
# sorted back by the paint key (z, sequence) — primitives cut under/over
# the plot at its sequence, a z=-1 shape emitted last sinks under
# everything, captions state the expected result. Checks the FFI library
# + python toolchain first; emits YDRAW_BIN envelopes on stdout (run
# inside yetty and compare the stacking to the captions).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/zorder.py"
