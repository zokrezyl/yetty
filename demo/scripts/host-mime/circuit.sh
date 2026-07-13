#!/bin/bash
# host-mime — circuit schematics drawn by the TERMINAL (ycircuit), not the
# client. The grid pitch tracks the terminal's cell height.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/circuit.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: circuit (terminal-side render) ===\n\n'
show "$ASSETS_ROOT/ycircuit/voltage-divider.circuit"
show "$ASSETS_ROOT/ycircuit/rc-lowpass.circuit"
show "$ASSETS_ROOT/ycircuit/555-blinker.circuit"
