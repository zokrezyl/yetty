#!/bin/bash
# host-mime — glTF binary meshes decoded by the TERMINAL (ymesh), not the
# client. Each file becomes one ymesh complex drawable.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/host-mime/mesh.sh
set -e
. "$(dirname "$0")/common.sh"

printf '=== host-mime: mesh (terminal-side GLB decode) ===\n\n'
show "$ASSETS_ROOT/ymesh/Box.glb"
show "$ASSETS_ROOT/ymesh/Duck.glb"
show "$ASSETS_ROOT/ymesh/Avocado.glb"
