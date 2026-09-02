#!/bin/bash
# ydraw FFI demo wrapper — runs python/twodialogs.py through the ydraw
# client-interface bindings: the SAME component instantiated twice with
# identical internal local ids (1/2) under distinct roots (500/501) — the
# paths [500.1] and [501.1] never collide (the case that used to need a
# producer namespace). Animates instance A in place while B stays untouched,
# then deletes A. Checks the FFI library + python toolchain first; emits
# YDRAW_BIN envelopes on stdout (run inside yetty).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/twodialogs.py"
