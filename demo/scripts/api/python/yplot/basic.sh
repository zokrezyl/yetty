#!/bin/bash
# yplot public API (Python) — object-oriented plotting through the FFI binding.
#
# Runs Python programs that import yetty.generated.api_yplot and build Plots
# with the generated OO API. show() emits the same yplot OSC envelope the
# standalone `yplot` tool does, so the plots draw inline in the host yetty
# session.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/api/python/yplot/basic.sh
#
# libyetty_ffi.so is produced by the normal build (make build-desktop-ytrace-release);
# it lives at build-desktop-ytrace-release/src/yetty/yffi/. The Python interpreter
# must be ABI-compatible with that system-built .so; the default is
# /usr/bin/python3. Override with PYTHON=... if needed (a Nix python3, for
# instance, cannot load a system-built .so).

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../../.." && pwd)"
ASSETS="$ROOT/demo/assets/api/python/yplot"
PYTHON="${PYTHON:-/usr/bin/python3}"
PAUSE="${DEMO_PAUSE:-0}"

# Locate the FFI library. runtime.py also auto-discovers it, but pass it
# explicitly so the demo fails loudly with a build hint if it is missing.
FFI_LIB="${YETTY_FFI_LIB:-}"
if [ -z "$FFI_LIB" ]; then
    FFI_LIB="$(ls "$ROOT"/build-desktop-*/src/yetty/yffi/libyetty_ffi.so 2>/dev/null | head -1 || true)"
fi
if [ -z "$FFI_LIB" ] || [ ! -f "$FFI_LIB" ]; then
    echo "libyetty_ffi.so not found — build it with 'make build-desktop-ytrace-release'" >&2
    exit 1
fi
if ! command -v "$PYTHON" >/dev/null 2>&1; then
    echo "python interpreter '$PYTHON' not found — set PYTHON=path/to/python3" >&2
    exit 1
fi

export PYTHONPATH="$ROOT/bindings/python${PYTHONPATH:+:$PYTHONPATH}"
export YETTY_FFI_LIB="$FFI_LIB"

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== yplot API (Python) — basic ===\n\n'
p
"$PYTHON" "$ASSETS/basic.py"
p

printf '\n=== yplot API (Python) — composable Functions ===\n\n'
p
"$PYTHON" "$ASSETS/composable.py"

printf '\n=== done ===\n'
