#!/bin/bash
# Seascape by TDM
# https://www.shadertoy.com/view/Ms2SD1
#
# Single Image pass, no texture channels — fetched with no API key, converted
# GLSL -> SPIR-V (glslang) -> WGSL (tint), and drawn straight into yetty.
#
# Prereq: a yetty listening on the RPC port (or let the harness launch one):
#   ./build-desktop-ytrace-release/yetty --rpc-port=9999 -e bash
# then, from another terminal:
#   ./demo/scripts/yshadertoy/02-seascape.sh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_common.sh"
st_ensure_yetty
st_demo Ms2SD1 "Seascape"
