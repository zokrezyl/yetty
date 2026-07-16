#!/bin/bash
# Raymarching - Primitives by iq
# https://www.shadertoy.com/view/Xds3zN
#
# Single Image pass, no texture channels — fetched with no API key, converted
# GLSL -> SPIR-V (glslang) -> WGSL (tint), and drawn straight into yetty.
#
# Prereq: a yetty listening on the RPC port (or let the harness launch one):
#   ./build-desktop-ytrace-release/yetty --rpc-port=9999 -e bash
# then, from another terminal:
#   ./demo/scripts/yshadertoy/04-raymarching-primitives.sh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_common.sh"
st_ensure_yetty
st_demo Xds3zN "Raymarching - Primitives"
