#!/usr/bin/env bash
# miniaudio 3rdparty wrapper. Noarch — single-header public-domain
# (MIT-0) audio I/O library. Same artefact serves every yetty target;
# the per-platform backend (WASAPI / CoreAudio / ALSA / PulseAudio /
# AAudio / OpenSL ES / WebAudio) is picked at compile time inside the
# header itself.
#
# Required env:
#   OUTPUT_DIR  where the tarball is written

set -euo pipefail
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

# Single ubuntu-friendly nix shell is enough — no compilation, just curl + tar.
SHELL_NAME="3rdparty-linux-x86_64"

if [ "${USE_NIX:-1}" = "0" ]; then
    exec bash "$(dirname "$0")/_build.sh" "$@"
fi

cd "$(dirname "$0")/../../.."
exec nix develop ".#$SHELL_NAME" --command bash build-tools/3rdparty/miniaudio/_build.sh "$@"
