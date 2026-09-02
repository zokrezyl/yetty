#!/bin/bash
# ydraw FFI demo wrapper — runs python/plotstream.py through the ydraw
# client-interface bindings: creates ONE plot with an addressable id and a
# named data buffer, then streams a scrolling sine into it with
# dlist.update(id, payload) — the SAME plot updates in place. Shows the
# per-complex `id` used to update plot data live. Checks the FFI library +
# python toolchain first; emits YDRAW_BIN envelopes on stdout (run inside
# yetty to watch the curve animate).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/plotstream.py"
