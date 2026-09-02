#!/bin/bash
# ydraw FFI demo wrapper — runs python/lifecycle.py through the ydraw
# client-interface bindings: the robustness rules live — anonymous content is
# cumulative, update/delete of a missing id are silent no-ops, a deleted id
# no-ops until re-used as fresh content, classical text written into a
# drawing's rows invalidates that insertion, and a plot scrolled fully into
# history seals (rendered, permanently un-addressable). Checks the FFI
# library + python toolchain first; emits YDRAW_BIN envelopes on stdout (run
# inside yetty).
source "$(dirname "$0")/../common.sh"
ffi_run_python "python/lifecycle.py"
