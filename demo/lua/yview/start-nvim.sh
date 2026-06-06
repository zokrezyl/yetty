#!/usr/bin/env bash
# Start neovim with the yview demo plugin loaded.
#
# Run this INSIDE a yetty terminal so the figure actually renders (the plugin
# writes its DCS envelopes to /dev/tty = the outer yetty). Build first:
#
#   make codegen && make ffi && make build-desktop-ffi-release
#
# Usage:
#   demo/lua/yview/start-nvim.sh [file ...]
#
# Then, inside nvim:
#   :YViewShow          overlay the current buffer as a scrollable figure
#   :YViewScroll 160    scroll the figure (negative = up)
#   :YViewClose         clear the figure
# Keymaps: <leader>vv show, <leader>vq close, <C-j>/<C-k> scroll.

set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"

# Point the bindings at the FFI shared library (yview.lua also falls back to
# this default, but exporting it makes the location explicit / overridable).
SO="${YETTY_FFI_LIB:-$ROOT/build-desktop-ffi-release/src/yetty/yffi/libyetty_ffi.so}"
if [ ! -f "$SO" ]; then
    echo "yview: FFI shared library not found at:" >&2
    echo "  $SO" >&2
    echo "build it with: make build-desktop-ffi-release" >&2
    exit 1
fi
export YETTY_FFI_LIB="$SO"

# Launch with an empty buffer (no file editing) and immediately render a yplot
# figure over the window. Pass an expression as args to override the default
# sin/cos, e.g.  start-nvim.sh 'tan(x)'
exec nvim -u "$DIR/init.lua" -c "YViewPlot $*"
