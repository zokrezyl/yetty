#!/bin/bash
# Shared preflight for the ydraw FFI demo wrappers (demo/scripts/ffi/ydraw).
#
# Sourced by the per-demo wrappers, not run directly. Locates the repo root
# and libyetty_ffi.so, verifies the language toolchain, then execs the demo
# from the repo root — the demos reference assets (demo/assets/yimage/...)
# relative to it. Each demo writes YDRAW_BIN envelopes to stdout; run the
# wrapper inside yetty to see the drawing scroll with the text.
#
# Environment:
#   YETTY_FFI_LIB      explicit path to libyetty_ffi.so (skips discovery)
#   YETTY_DEMO_PYTHON  python interpreter override (default: /usr/bin/python3
#                      when present — sandboxed interpreters like a nix
#                      profile python often cannot dlopen a system-built
#                      libyetty_ffi.so, e.g. libstdc++.so.6 not found)

set -e

FFI_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFI_ROOT="$(cd "$FFI_COMMON_DIR/../../../.." && pwd)"
FFI_DEMO_DIR="$FFI_ROOT/demo/ffi/ydraw"

ffi_find_library() {
    if [ -n "$YETTY_FFI_LIB" ]; then
        if [ ! -f "$YETTY_FFI_LIB" ]; then
            echo "YETTY_FFI_LIB is set but does not exist: $YETTY_FFI_LIB" >&2
            return 1
        fi
        FFI_LIB="$YETTY_FFI_LIB"
        return 0
    fi
    # macOS builds emit libyetty_ffi.dylib; Linux/BSD emit .so. Try both.
    local basenames=(libyetty_ffi.so libyetty_ffi.dylib)
    local tree name
    for tree in "$FFI_ROOT"/build-desktop-*-release "$FFI_ROOT"/build-desktop-*; do
        for name in "${basenames[@]}"; do
            local candidate="$tree/src/yetty/yffi/$name"
            if [ -f "$candidate" ]; then
                FFI_LIB="$candidate"
                return 0
            fi
        done
    done
    echo "libyetty_ffi.{so,dylib} not found in any build-desktop-* tree." >&2
    echo "Build it:  USE_DISTCC=1 make build-desktop-ytrace-release" >&2
    echo "or point YETTY_FFI_LIB at an existing library." >&2
    return 1
}

ffi_require_tool() {
    local tool="$1"
    local hint="$2"
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "'$tool' not found on \$PATH — $hint" >&2
        exit 1
    fi
}

ffi_run_python() {
    ffi_find_library
    local python_bin="${YETTY_DEMO_PYTHON:-}"
    if [ -z "$python_bin" ]; then
        # Prefer the system interpreter: it shares the loader environment
        # the library was built for. A PATH python from an isolated
        # toolchain (nix, conda) frequently fails the dlopen.
        if [ -x /usr/bin/python3 ]; then
            python_bin=/usr/bin/python3
        else
            python_bin=python3
        fi
    fi
    if ! command -v "$python_bin" > /dev/null 2>&1; then
        echo "'$python_bin' not found — install Python 3 or set YETTY_DEMO_PYTHON" >&2
        exit 1
    fi
    cd "$FFI_ROOT"
    exec env PYTHONPATH="$FFI_ROOT/bindings/python" YETTY_FFI_LIB="$FFI_LIB" \
        "$python_bin" "$FFI_DEMO_DIR/$1"
}

ffi_run_lua() {
    ffi_find_library
    ffi_require_tool luajit "install LuaJIT (the bindings use the LuaJIT ffi module)"
    cd "$FFI_ROOT"
    exec env LUA_PATH="$FFI_ROOT/bindings/lua/?.lua;;" YETTY_FFI_LIB="$FFI_LIB" \
        luajit "$FFI_DEMO_DIR/$1"
}

ffi_run_typescript() {
    ffi_find_library
    ffi_require_tool node "install Node.js >= 23 (the demos run as .ts via type stripping)"
    if ! node -e 'process.exit(Number(process.versions.node.split(".")[0]) >= 23 ? 0 : 1)'; then
        echo "Node.js >= 23 required to run .ts directly (found $(node --version))." >&2
        exit 1
    fi
    if [ ! -d "$FFI_DEMO_DIR/typescript/node_modules" ]; then
        echo "npm dependencies missing — run:  (cd demo/ffi/ydraw/typescript && npm install)" >&2
        exit 1
    fi
    cd "$FFI_ROOT"
    exec env YETTY_FFI_LIB="$FFI_LIB" node "$FFI_DEMO_DIR/$1"
}

ffi_run_go() {
    ffi_find_library
    ffi_require_tool go "install Go >= 1.21 (the bindings use cgo)"
    local library_dir
    library_dir="$(dirname "$FFI_LIB")"
    # macOS uses DYLD_LIBRARY_PATH; Linux/BSD use LD_LIBRARY_PATH. Set both
    # so the dlopen search hits the fresh build regardless of platform.
    cd "$FFI_ROOT"
    exec env CGO_LDFLAGS="-L$library_dir -lyetty_ffi" \
        LD_LIBRARY_PATH="$library_dir" DYLD_LIBRARY_PATH="$library_dir" \
        go run "$FFI_DEMO_DIR/$1"
}
