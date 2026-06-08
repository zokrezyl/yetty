#!/usr/bin/env bash
#
# Launch nvim with the yetty demo plugin loaded.
#
# Run this INSIDE a yetty terminal: :YettyGraph / :YettyShow / :YettyDashboard
# forward their figures to the host terminal through nvim's stderr channel, and
# only yetty renders them. In any other terminal the plugin loads fine but the
# figures have nowhere to draw.
#
# Usage:
#   demo/nvim/run.sh              # load the plugin, land in nvim
#   demo/nvim/run.sh file.txt     # ...and open file.txt (extra args go to nvim)
#
# Then, inside nvim:
#   :YettyGraph sin(x)*cos(2*x)   plot an expression over the current window
#   :YettyShow                    render the current buffer as a figure
#   :YettyDashboard               text + two plots in a float
#   :YettyScroll 60               scroll the most recent figure
#   :YettyClear                   remove them
set -euo pipefail

# This script lives in <root>/demo/nvim, which is also the plugin's runtimepath
# entry (it holds plugin/ and lua/).
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"

# :YettyViews needs the FFI shared library; warn early if it is missing.
ffi_lib="$root/build-desktop-ffi-release/src/yetty/yffi/libyetty_ffi.so"
if [ ! -f "$ffi_lib" ]; then
  echo "yetty: libyetty_ffi.so not found at $ffi_lib" >&2
  echo "       :YettyViews will not work until you build it:" >&2
  echo "       USE_DISTCC=1 make build-desktop-ffi-release" >&2
fi

# -u NORC: skip the user's init but still source rtp plugins, so plugin/yetty.lua
# registers cleanly. --cmd runs before plugins load, so the rtp entry is in place
# when plugin/yetty.lua is sourced.
exec nvim -u NORC --cmd "set runtimepath+=$here" "$@"
