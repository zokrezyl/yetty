#!/bin/bash
# ydraw FFI demo wrapper — runs lua/groups.lua through the ydraw
# client-interface bindings: named entity groups replaced in place, deleted
# in a timed loop, and re-added as fresh content. Checks the FFI library +
# LuaJIT toolchain first; emits YDRAW_BIN envelopes on stdout (run inside
# yetty to watch the panel animate, vanish, and cycle).
source "$(dirname "$0")/../common.sh"
ffi_run_lua "lua/groups.lua"
