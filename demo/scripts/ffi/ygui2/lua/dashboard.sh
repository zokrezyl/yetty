#!/bin/bash
# ygui2 FFI demo wrapper — runs lua/dashboard.lua through the ygui2 widget
# toolkit bindings. Interactive PTY client: run INSIDE a yetty pane
# (mouse + keys are forwarded to the app; Ctrl-C quits).
source "$(dirname "$0")/../common.sh"
ffi_run_lua "lua/dashboard.lua"
